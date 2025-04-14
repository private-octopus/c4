/*
* Author: Christian Huitema
* Copyright (c) 2019, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "picoquic_internal.h"
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

/* C4 algorithm is a work in progress. We start with some simple principles:
* - Track delays, but this expose issue when competing with Cubic
* - Compete with Cubic: fallback to BBRv1, best of last 6 epochs, ignore delays, losses.
* - Stopping the compete mode? When the delay becomes acceptable?
* - App limited mode: freeze the parameters? Do probe on search?
* - Probing mode: probe for one RTT, recover for one RTT, assess after recover.
* - Probing interval: Fibonacci sequence, up to 16 or 17? 1, 1, 2, 3, 5, 8, 13
* - Tuning the interval: no bw increase => larger, bw increase => sooner, how soon?
* - stopping the compete mode: ECN? ECN as signal that the bottleneck is actively managed.
* - compete with self? Maybe introduce some random behavior.
* - suspension mode: support the suspension detected, etc.
 */


#define PICOQUIC_CC_ALGO_NUMBER_C4 8
#define C4_DELAY_THRESHOLD_MAX 25000
#define MULT1024(c, v) (((v)*(c)) >> 10)
#define C4_ALPHA_RECOVER_1024 921 /* 90% */
#define C4_ALPHA_CRUISE_1024 1003 /* 98% */
#define C4_ALPHA_PUSH_1024 1280 /* 125 % */
#define C4_ALPHA_INITIAL 2048 /* 200% */
#define C4_BETA_1024 128 /* 0.125 */
#define C4_BETA_LOSS_1024 256 /* 25%, 1/4th */
#define C4_BETA_INITIAL_1024 512 /* 50% */
#define C4_NB_PUSH_BEFORE_RESET 3
#define C4_REPEAT_THRESHOLD 4

/* States of C4:
* 
* Initial.   Similar to Hystart for now, as a place holder.
* Recovery.  After an event, freeze the parameters for one RTT. This is the time
*            to measure whether a previous push was a success.
* Cruising.  For N intervals. Be ready to notice congestion, or change in conditions.
*            We should define N as x*log(cwin / cwin_min), so connections sending
*            lots of data wait a bit longer, which should improve fairness.
*            Question: is this "N intervals" or "some amount of data sent"?
*            the latter is better for the fairness issue.
* Pushing.   For one RTT. Transmit at higher rate, to probe the network, then
*            move to "cruising". Higher rate should be 25% higher, to probe
*            without creating big queues.
* Suspended. If feedback is lost.
* 
* Competing. If we detect the need to compete with Cubic. We probably want to
*            do something. Substate compete cruise, compete push, compute recovery.
*            is competing a state or a flag? (just disable delay feedback?)
* 
* Transitions:
*            initial to recovery -- similar to hystart for now.
*            recovery to initial -- if measurements show increase in data rate compared to era.
*            recovery to cruising -- at the end of period.
*            cruising, pushing to recovery -- if excess delay, loss or ECN
*            pushing to recovery -- at end of period.
*            any state to suspended -- if suspension event.
*            to competing -- if some number of entering recovery because delay too high?
*            competing to recovery? or to initial? if the delay decreases enough?
*            if 3 successful push pahses -- transit to initial
* 
* 
* State variables:
* - CWIN. Main control variable.
* - Pacing_rate.
* - Sequence number of first packet sent in epoch. Epoch ends when this is acknowledged.
* - Observed data rate. Measured at the end of epoch N, reflects setting at epoch N-1.
* - Average rate of EC marking
* - Average rate of Packet loss
* - Average rate of excess delay
* - Number of cruising bytes sent.
* - Cruising bytes target before transition to push
* - value of nominal CWIN before suspension
* - RTT min
 */

typedef enum {
    c4_initial = 0,
    c4_recovery,
    c4_cruising,
    c4_pushing,
    c4_suspended,
    c4_competing
} c4_alg_state_t;

typedef struct st_c4_state_t {
    c4_alg_state_t alg_state;
    uint64_t nominal_cwin;
    uint64_t alpha_1024_current;
    uint64_t era_sequence; /* sequence number of first packet in era */
    uint64_t cruise_bytes_ack; /* accumulate bytes count in cruise state */
    uint64_t cruise_bytes_target; /* expected bytes count until end of cruise */
    int increased_during_era;
    int nb_eras_no_increase;
    int nb_push_no_congestion; /* Nalphaumber of successive 25% pushes with no congestion */

    uint64_t rtt_min;
    uint64_t delay_threshold;
    uint64_t rolling_rtt_min; /* Min RTT measured for this epoch */
    uint64_t suspended_nominal_cwin;
    uint64_t suspended_nominal_state;
    int nb_cc_events;
    unsigned int last_freeze_was_timeout : 1;
    unsigned int last_freeze_was_not_delay : 1;
    unsigned int rtt_min_is_trusted : 1;
    picoquic_min_max_rtt_t rtt_filter;
} c4_state_t;

static void c4_enter_recovery(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    int is_congested,
    int is_delay,
    int is_timeout);

/* Compute the delay threshold for declaring congestion,
* as the min of RTT/8 and c4_DELAY_THRESHOLD_MAX (25 ms) 
 */

uint64_t c4_delay_threshold(uint64_t rtt_min)
{
    uint64_t delay = rtt_min / 8;
    if (delay > C4_DELAY_THRESHOLD_MAX) {
        delay = C4_DELAY_THRESHOLD_MAX;
    }
    return delay;
}

/* Compute the base 2 logarithm of an uint64 number. 
* This is used to compute the number of bytes to wait for until
* exit cruising
 */
static uint64_t c4_uint64_log_exp(uint64_t v)
{
    uint64_t l = 0;
    if (v > 0xFFFFFFFFull) {
        l += 32;
        v >>= 32;
    }
    if (v > 0xFFFFull) {
        l += 16;
        v >>= 16;
    }
    if (v > 0xFFull) {
        l += 8;
        v >>= 8;
    }
    if (v > 0x0Full) {
        l += 4;
        v >>= 4;
    }
    if (v > 0x3ull) {
        l += 2;
        v >>= 2;
    }
    if (v > 0x1ull) {
        l += 1;
    }
    return l;
}

static uint64_t c4_uint64_log_decpart(uint64_t dec_1024)
{
    uint64_t log_decpart = 0;

    dec_1024 += 1024;

    if (dec_1024 >= 1449) {
        log_decpart += 512;
        dec_1024 = (dec_1024 * 1024) / 1449;
    }
    if (dec_1024 >= 1218) {
        log_decpart += 256;
        dec_1024 = (dec_1024 * 1024) / 1218;
    }
    if (dec_1024 >= 1117) {
        log_decpart += 128;
        dec_1024 = (dec_1024 * 1024) / 1117;
    }
    if (dec_1024 >= 1070) {
        log_decpart += 64;
        dec_1024 = (dec_1024 * 1024) / 1070;
    }
    if (dec_1024 >= 1047) {
        log_decpart += 32;
        dec_1024 = (dec_1024 * 1024) / 1047;
    }
    dec_1024 += (((dec_1024 - 1024) * 1477) / 1024);
    return dec_1024;
}

uint64_t c4_uint64_log_1024(uint64_t v)
{
    uint64_t l_exp = c4_uint64_log_exp(v);
    uint64_t decimal_part = v;
    uint64_t l_1024 = l_exp << 10;

    if (l_exp > 10) {
        decimal_part >>= (l_exp - 10);
    }
    else if (l_exp < 10) {
        decimal_part <<= (10 - l_exp);
    }
    decimal_part &= 1023;

    l_1024 += c4_uint64_log_decpart(decimal_part);

    return l_1024;
}

/* Compute the cruising interval, as a function of the window.
* We want the number of RTT to grow as the log of the window,
* with an extreme coefficient 1 if the window is < 2096 and 
* 8 if the window is > 2^28 bytes.
* the formula is 1 + 7*(x-11)/(28 -11)
 */
uint64_t c4_cruise_bytes_target(uint64_t w)
{
    uint64_t x_1024;
    uint64_t l_1024 = c4_uint64_log_1024(w);
    uint64_t target = w;

    if (l_1024 > 28*1024) {
        l_1024 = 28*1024;
    }
    else if (l_1024 < 11*1024) {
        l_1024 = 11*1024;
    }
    x_1024 = 1024 + (7*(l_1024 - 11*1024))/((28-11)*1024);
    target += MULT1024(x_1024, w);
    return target;
}

/* On an ACK event, compute the corrected value of the number of bytes delivered */
static uint64_t c4_compute_corrected_delivered_bytes(c4_state_t* c4_state, uint64_t nb_bytes_delivered, uint64_t rtt_measurement)
{
    uint64_t duration_max = MULT1024(1024 + 51, c4_state->rtt_min);
    if (rtt_measurement > duration_max) {
        uint64_t ratio_1024 = (duration_max * 1024) / rtt_measurement;
        nb_bytes_delivered = MULT1024(ratio_1024, nb_bytes_delivered);
    }
    return nb_bytes_delivered;
}

/* End of round trip.
* Happens if packet waited for is acked.
* Add bandwidth measurement to bandwidth barrel.
 */
static int c4_era_check(
    picoquic_path_t* path_x,
    c4_state_t* c4_state)
{
    return (picoquic_cc_get_ack_number(path_x->cnx, path_x) >= c4_state->era_sequence);
}

static void c4_era_reset(
    picoquic_path_t* path_x,
    c4_state_t* c4_state)
{
    c4_state->era_sequence = picoquic_cc_get_sequence_number(path_x->cnx, path_x);
    c4_state->increased_during_era = 0;
}

static void c4_enter_initial(picoquic_path_t* path_x, c4_state_t* c4_state, uint64_t current_time)
{
    c4_state->alg_state = c4_initial;
    c4_state->nb_push_no_congestion = 0;
    c4_state->alpha_1024_current = C4_ALPHA_INITIAL;
    path_x->cwin = MULT1024(c4_state->alpha_1024_current, c4_state->nominal_cwin);
    c4_era_reset(path_x, c4_state);
    c4_state->nb_eras_no_increase = 0;
}

void c4_reset(c4_state_t* c4_state, picoquic_path_t* path_x, uint64_t current_time)
{
    memset(c4_state, 0, sizeof(c4_state_t));
    c4_state->rtt_min = path_x->smoothed_rtt;
    c4_state->rolling_rtt_min = c4_state->rtt_min;
    c4_state->nominal_cwin = PICOQUIC_CWIN_INITIAL;
    c4_enter_initial(path_x, c4_state, current_time);
}

void c4_seed_cwin(c4_state_t* c4_state, picoquic_path_t* path_x, uint64_t bytes_in_flight)
{
    if (c4_state->alg_state == c4_initial) {
        if (path_x->cwin < bytes_in_flight) {
            path_x->cwin = bytes_in_flight;
        }
    }
}

static void c4_exit_initial(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_congestion_notification_t notification)
{
    /* We assume that any required correction is done prior to calling this */
    if (notification != picoquic_congestion_notification_acknowledgement) {
        c4_state->nominal_cwin = path_x->cwin;
    }
    c4_enter_recovery(path_x, c4_state, 0, 0, 0);
}

static void c4_initial_handle_rtt(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_congestion_notification_t notification, uint64_t rtt_measurement, uint64_t current_time)
{
    /* if in slow start, increase the window for long delay RTT */
    c4_state->rtt_min = path_x->rtt_min;

    if (path_x->rtt_min > PICOQUIC_TARGET_RENO_RTT /* && cubic_state->ssthresh == UINT64_MAX */ ) {
        path_x->cwin = picoquic_cc_update_cwin_for_long_rtt(path_x);
    }

    /* HyStart. */
    /* Using RTT increases as congestion signal. This is used
     * for getting out of slow start, but also for ending a cycle
     * during congestion avoidance */
    if (picoquic_cc_hystart_test(&c4_state->rtt_filter, rtt_measurement,
        path_x->pacing.packet_time_microsec, current_time, 0)) {

        if (c4_state->rtt_filter.rtt_filtered_min > PICOQUIC_TARGET_RENO_RTT) {
            uint64_t beta_1024;

            if (c4_state->rtt_filter.rtt_filtered_min > PICOQUIC_TARGET_SATELLITE_RTT) {
                beta_1024 = ((uint64_t)PICOQUIC_TARGET_SATELLITE_RTT*1024) / c4_state->rtt_filter.rtt_filtered_min;
            }
            else {
                beta_1024 = ((uint64_t)PICOQUIC_TARGET_RENO_RTT*1024) / c4_state->rtt_filter.rtt_filtered_min;
            }

            uint64_t base_window = MULT1024(beta_1024, path_x->cwin);
            uint64_t delta_window = path_x->cwin - base_window;
            path_x->cwin -= (delta_window / 2);
        }
        else {
            /* In the general case, compensate for the growth of the window after the acknowledged packet was sent. */
            path_x->cwin /= 2;
        }

        c4_exit_initial(path_x, c4_state, notification);
    }
}

static void c4_initial_handle_loss(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_congestion_notification_t notification)
{
    path_x->cwin = MULT1024(C4_BETA_INITIAL_1024, path_x->cwin);
    c4_exit_initial(path_x, c4_state, notification);
}

static void c4_initial_handle_ack(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_per_ack_state_t* ack_state)
{
    if (c4_era_check(path_x, c4_state)) {
        /* Only exit on lack of increase if not app limited */
        if (!c4_state->increased_during_era && path_x->last_time_acked_data_frame_sent > path_x->last_sender_limited_time) {
            c4_state->nb_eras_no_increase++;
        }
        c4_era_reset(path_x, c4_state);
    }
    /* TODO: handle possible information about bandwidth seed, peak bandwidth, careful resume,
     * as required to support geo satellites.
     */
    if (c4_state->nb_eras_no_increase >= 3) {
        c4_exit_initial(path_x, c4_state, picoquic_congestion_notification_acknowledgement);
    }
    else {
        /* Increase cwin based on bandwidth estimation. */
        path_x->cwin = picoquic_cc_update_target_cwin_estimation(path_x);
    }
}

void c4_init(picoquic_cnx_t * cnx, picoquic_path_t* path_x, char const* option_string, uint64_t current_time)
{
    /* Initialize the state of the congestion control algorithm */
    c4_state_t* c4_state = path_x->congestion_alg_state;
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(cnx);
    UNREFERENCED_PARAMETER(option_string);
#endif
    
    if (c4_state == NULL) {
        c4_state = (c4_state_t*)malloc(sizeof(c4_state_t));
    }
    
    if (c4_state != NULL){
        cnx->is_lost_feedback_notification_required = 1;
        c4_reset(c4_state, path_x, current_time);
    }

    path_x->congestion_alg_state = (void*)c4_state;
}

/*
* Enter recovery.
* CWIN is set to C4_ALPHA_RECOVER of nominal value (90%)
* Remember the first no ACK packet -- recovery will end when that
* packet is acked.
*/
static void c4_enter_recovery(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    int is_congested,
    int is_delay,
    int is_timeout)
{
    if (!is_congested) {
        c4_state->last_freeze_was_not_delay = 0;
        c4_state->last_freeze_was_timeout = 0;
    }
    else {
        c4_state->nb_push_no_congestion = 0;
        c4_state->last_freeze_was_not_delay = !is_delay;
        c4_state->last_freeze_was_timeout = is_timeout;
    }
    c4_state->alg_state = c4_recovery;
    c4_state->alpha_1024_current = C4_ALPHA_RECOVER_1024;
    path_x->cwin = MULT1024(C4_ALPHA_RECOVER_1024, c4_state->nominal_cwin);
    c4_era_reset(path_x, c4_state);
}

/* Enter cruise.
* CWIN is set C4_ALPHA_CRUISE of nominal value (98%?)
* Ack target if set to nominal cwin times log2 of cwin.
*/
static void c4_enter_cruise(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_era_reset(path_x, c4_state);
    c4_state->cruise_bytes_ack = 0;
    c4_state->cruise_bytes_target = c4_cruise_bytes_target(c4_state->nominal_cwin);
    c4_state->alpha_1024_current = C4_ALPHA_CRUISE_1024;
    path_x->cwin = MULT1024(C4_ALPHA_CRUISE_1024, c4_state->nominal_cwin);
    c4_state->alg_state = c4_cruising;
}

/* Enter push.
* CWIN is set C4_ALPHA_PUSH of nominal value (125%?)
* Ack target if set to nominal cwin times log2 of cwin.
*/
static void c4_enter_push(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_state->nb_push_no_congestion++;
    c4_state->alpha_1024_current = C4_ALPHA_PUSH_1024;
    path_x->cwin = MULT1024(C4_ALPHA_PUSH_1024, c4_state->nominal_cwin);
    if (path_x->cwin < c4_state->nominal_cwin + path_x->send_mtu) {
        path_x->cwin = c4_state->nominal_cwin + path_x->send_mtu;
    }
    c4_era_reset(path_x, c4_state);
    c4_state->alg_state = c4_pushing;
}

/* Enter and exit suspension.
* When entering suspension, we set the cwin to the bytes in flight,
* which will stop transmission of new packets on the path. We remember
* the nominal CWIN so it could be restored. On exit, we enter the
* recovery state with the new CWIN */
static void c4_enter_suspended(
    picoquic_path_t* path_x,
    c4_state_t* c4_state)
{
    c4_state->alpha_1024_current = C4_ALPHA_RECOVER_1024;
    c4_state->suspended_nominal_cwin = c4_state->nominal_cwin;
    c4_state->suspended_nominal_state = c4_state->alg_state;
    c4_state->alg_state = c4_suspended;
    path_x->cwin = path_x->bytes_in_transit;
}

static void c4_exit_suspended(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_state->nominal_cwin = c4_state->suspended_nominal_cwin;
    c4_enter_recovery(path_x, c4_state, 0, 0, 0);
}

/* Handle data ack event.
 */
void c4_handle_ack(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    uint64_t corrected_delivered_bytes = c4_compute_corrected_delivered_bytes(c4_state, ack_state->nb_bytes_delivered_since_packet_sent, ack_state->rtt_measurement);

    if (corrected_delivered_bytes > c4_state->nominal_cwin) {
        c4_state->nominal_cwin = corrected_delivered_bytes;
        path_x->cwin = MULT1024(c4_state->alpha_1024_current, c4_state->nominal_cwin);
        c4_state->increased_during_era = 1;
        c4_state->nb_eras_no_increase = 0;
    }

    c4_state->cruise_bytes_ack += ack_state->nb_bytes_acknowledged;

    if (c4_state->alg_state == c4_initial) {
        c4_initial_handle_ack(path_x, c4_state, ack_state);
    }
    else {
        if (c4_era_check(path_x, c4_state)) {
            /* we could set a bandwidth estimate, but we would rather use the main code's estimate */

            switch (c4_state->alg_state) {
            case c4_recovery:
                c4_state->nb_cc_events = 0;
                c4_enter_cruise(path_x, c4_state, current_time);
                break;
            case c4_cruising:
                c4_era_reset(path_x, c4_state);
                break;
            case c4_pushing:
                c4_enter_recovery(path_x, c4_state, 0, 0, 0);
                break;
            case c4_suspended:
                c4_exit_suspended(path_x, c4_state, current_time);
                break;
            default:
                c4_era_reset(path_x, c4_state);
                break;
            }
        }

        if (c4_state->alg_state == c4_cruising) {
            if (c4_state->nb_push_no_congestion >= C4_NB_PUSH_BEFORE_RESET) {
                c4_enter_initial(path_x, c4_state, current_time);
            }
            else if (c4_state->cruise_bytes_ack >= c4_state->cruise_bytes_target) {
                c4_enter_push(path_x, c4_state, current_time);
            }
        }
    }
}

/* Reaction to ECN/CE or sustained losses.
 * This is more or less the same code as added to bbr.
 * This code is called if an ECN/EC event is received, or a timeout
 * event, or a lost event indicating a high loss rate,
 * or a delay event.
 * 
 * TODO: proper treatment of ECN per L4S
 */
static void c4_notify_congestion(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t rtt_latest,
    int is_delay,
    int is_timeout)
{
    uint64_t beta = C4_BETA_LOSS_1024;
    c4_state->nb_push_no_congestion = 0;

    if (c4_state->alg_state == c4_recovery &&
        (!is_timeout || !c4_state->last_freeze_was_timeout) &&
        (!is_delay || !c4_state->last_freeze_was_not_delay)) {
        /* Do not treat additional events during same freeze interval */
        return;
    }

    if (c4_state->alg_state == c4_suspended /* && !is_timeout */) {
        return;
    }

    c4_state->nb_cc_events = 0;

    if (is_delay) {
        /* TODO: we should really use bytes in flight! */
        beta = C4_BETA_1024;
        if (rtt_latest > c4_state->rtt_min) {
            beta = (1024 * (rtt_latest - c4_state->rtt_min)) / c4_state->rtt_min;
        }
        if (c4_state->alg_state == c4_initial && beta < C4_BETA_INITIAL_1024) {
            beta = C4_BETA_INITIAL_1024;
        }
    }
    c4_state->nominal_cwin -= MULT1024(beta, c4_state->nominal_cwin);

    if (is_timeout || c4_state->nominal_cwin < PICOQUIC_CWIN_MINIMUM) {
        c4_state->nominal_cwin = PICOQUIC_CWIN_MINIMUM;
    }

    c4_enter_recovery(path_x, c4_state, 1, is_delay, is_timeout);

    picoquic_update_pacing_data(path_x->cnx, path_x, 0);

    path_x->is_ssthresh_initialized = 1;
}


static void c4_handle_rtt(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t rtt_measurement,
    uint64_t current_time)
{
    uint64_t delta_rtt = 0;

    picoquic_cc_filter_rtt_min_max(&c4_state->rtt_filter, rtt_measurement);

    if (c4_state->rtt_filter.is_init) {
        /* We use the maximum of the last samples as the candidate for the
         * min RTT, in order to filter the rtt jitter */
        if (c4_state->rtt_filter.sample_max < c4_state->rolling_rtt_min || c4_state->rolling_rtt_min == 0) {
            /* If not end of epoch, update the rolling minimum */
            c4_state->rolling_rtt_min = c4_state->rtt_filter.sample_max;
            if (c4_state->rolling_rtt_min < c4_state->rtt_min) {
                c4_state->rtt_min = c4_state->rolling_rtt_min;
            }
        }
    }
    if (rtt_measurement < c4_state->rtt_min) {
        c4_state->delay_threshold = c4_delay_threshold(c4_state->rtt_min);
    }
    else if (c4_state->rtt_min_is_trusted) {
        delta_rtt = rtt_measurement - c4_state->rtt_min;
    }
    else {
        c4_state->rtt_min = rtt_measurement;
        c4_state->rolling_rtt_min = rtt_measurement;
        c4_state->rtt_min_is_trusted = 1;
        delta_rtt = 0;
    }

    if (delta_rtt < c4_state->delay_threshold) {
        c4_state->nb_cc_events = 0;
    }
    else {
        /* May well be congested */
        c4_state->nb_cc_events++;
        if (c4_state->nb_cc_events >= C4_REPEAT_THRESHOLD) {
            /* Too many events, reduce the window */
            c4_notify_congestion(path_x, c4_state, rtt_measurement, 1, 0);
        }
    }
}

/*
 * Properly implementing c4 requires managing a number of
 * signals, such as packet losses or acknowledgements. We attempt
 * to condensate all that in a single API, which could be shared
 * by many different congestion control algorithms.
 */
void c4_notify(
    picoquic_cnx_t* cnx, picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t * ack_state,
    uint64_t current_time)
{
    c4_state_t* c4_state = (c4_state_t*)path_x->congestion_alg_state;
    path_x->is_cc_data_updated = 1;

    if (c4_state != NULL) {
        switch (notification) {
        case picoquic_congestion_notification_acknowledgement:
            c4_handle_ack(path_x, c4_state, ack_state, current_time);
            picoquic_update_pacing_data(cnx, path_x, c4_state->alg_state == c4_initial);
            break;
        case picoquic_congestion_notification_ecn_ec:
            /* TODO: ECN is special? Implement the prague logic */
            if (c4_state->alg_state == c4_initial) {
                c4_initial_handle_loss(path_x, c4_state, notification);
            }
            else {
                c4_notify_congestion(path_x, c4_state, 0, 0, 0);
            }
            break;
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_timeout:
            if (picoquic_cc_hystart_loss_test(&c4_state->rtt_filter, notification, ack_state->lost_packet_number, PICOQUIC_SMOOTHED_LOSS_THRESHOLD)) {

                if (c4_state->alg_state == c4_initial) {
                    c4_initial_handle_loss(path_x, c4_state, notification);
                }
                else {
                    c4_notify_congestion(path_x, c4_state, 0, 0,
                        (notification == picoquic_congestion_notification_timeout) ? 1 : 0);
                }
            }
            break;
        case picoquic_congestion_notification_spurious_repeat:
            if (c4_state->nb_cc_events > 0) {
                c4_state->nb_cc_events--;
            }
            break;
        case picoquic_congestion_notification_rtt_measurement:
            if (c4_state->alg_state == c4_initial) {
                c4_initial_handle_rtt(path_x, c4_state, notification, ack_state->rtt_measurement, current_time);
            }
            else {
                c4_handle_rtt(cnx, path_x, c4_state, ack_state->rtt_measurement, current_time);
            }
            picoquic_update_pacing_data(cnx, path_x, c4_state->alg_state == c4_initial);
            break;
        case picoquic_congestion_notification_lost_feedback:
            if (c4_state->rtt_min_is_trusted && c4_state->alg_state != c4_initial && c4_state->alg_state != c4_suspended) {
                c4_enter_suspended(path_x, c4_state);
            }
            break;
        case picoquic_congestion_notification_cwin_blocked:
            break;
        case picoquic_congestion_notification_reset:
            c4_reset(c4_state, path_x, current_time);
            break;
        case picoquic_congestion_notification_seed_cwin:
            c4_seed_cwin(c4_state, path_x, ack_state->nb_bytes_acknowledged);
            break;
        default:
            /* ignore */
            break;
        }
    }
}

/* Release the state of the congestion control algorithm */
void c4_delete(picoquic_path_t* path_x)
{
    if (path_x->congestion_alg_state != NULL) {
        free(path_x->congestion_alg_state);
        path_x->congestion_alg_state = NULL;
    }
}


/* Observe the state of congestion control */

void c4_observe(picoquic_path_t* path_x, uint64_t* cc_state, uint64_t* cc_param)
{
    c4_state_t* c4_state = (c4_state_t*)path_x->congestion_alg_state;
    *cc_state = (uint64_t)c4_state->alg_state;
    *cc_param = c4_state->rolling_rtt_min;
}

/* Definition record for the FAST CC algorithm */

#define c4_ID "c4" 

picoquic_congestion_algorithm_t c4_algorithm_struct = {
    c4_ID, PICOQUIC_CC_ALGO_NUMBER_C4,
    c4_init,
    c4_notify,
    c4_delete,
    c4_observe
};

picoquic_congestion_algorithm_t* c4_algorithm = &c4_algorithm_struct;
