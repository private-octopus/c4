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
#define C4_BETA_1024 128 /* 0.125 */
#define C4_BETA_LOSS_1024 256 /* 25%, 1/4th */
#define C4_NB_PUSH_BEFORE_RESET 3
#define C4_REPEAT_THRESHOLD 4

/* States of C4:
* 
* Initial.   Probably use Hystart for now, as a place holder.
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
* 
* Control variable:
*            when competing, largest data rate of last N periods times RTT
* 
* State variables:
* - CWIN. Main control variable.
* - Pacing_rate.
* - Sequence number of first packet sent in epoch. Epoch ends when this is acknowledged.
* - Observed data rate. Measured at the end of epoch N, reflects setting at epoch N-1.
* - Average rate of EC marking
* - Average rate of Packet loss
* - Average rate of excess delay
* - Number of cruising epochs / number of cruising bytes sent.
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
    uint64_t era_bytes_ack; /* accumulate byte count until RTT measured */
    uint64_t era_time_stamp; /* time of last RTT */
    uint64_t era_sequence; /* sequence number of first packet in era */
    uint64_t cruise_bytes_ack; /* accumulate bytes count in cruise state */
    uint64_t cruise_bytes_target; /* expected bytes count until end of cruise */
    int nb_push_no_congestion; /* Number of successive 25% pushes with no congestion */

    uint64_t rtt_min;
    uint64_t delay_threshold;
    uint64_t rolling_rtt_min; /* Min RTT measured for this epoch */
    int nb_cc_events;
    unsigned int last_freeze_was_timeout : 1;
    unsigned int last_freeze_was_not_delay : 1;
    unsigned int rtt_min_is_trusted : 1;
    picoquic_min_max_rtt_t rtt_filter;
} c4_state_t;

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
uint64_t c4_uint64_log(uint64_t v)
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

/* Compute the cruising interval, as a function of the window.
* We want the number of RTT to grow as the log of the window,
* with an extreme coefficient 1 if the window is < 2096 and 
* 8 if the window is > 2^28 bytes.
* the formula is 1 + 7*(x-11)/(28 -11)
* we compute using 7*1024/(28 -11) ~= 422
 */
uint64_t c4_cruise_bytes_target(uint64_t w)
{
    uint64_t x_1024;
    uint64_t l = c4_uint64_log(w);
    uint64_t target = w;


    if (l > 28) {
        l = 28;
    }
    else if (l < 11) {
        l = 11;
    }
    x_1024 = (l - 11) * 422;
    target += MULT1024(x_1024, w);
    return target;
}

/* End of round trip.
* Happens if packet waited for is acked.
* Add bandwidth measurement to bandwidth barrel.
 */
static int c4_era_check(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state)
{
    return (picoquic_cc_get_ack_number(cnx, path_x) >= c4_state->era_sequence);
}

static void c4_era_reset(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_state->era_sequence = picoquic_cc_get_sequence_number(cnx, path_x);
    c4_state->era_bytes_ack = 0; /* accumulate byte count until RTT measured */
    c4_state->era_time_stamp = current_time; /* time of last RTT */
}

static void c4_enter_initial(picoquic_cnx_t* cnx, picoquic_path_t* path_x, c4_state_t* c4_state, uint64_t current_time)
{
    c4_state->alg_state = c4_initial;
    c4_state->nb_push_no_congestion = 0;
    c4_state->delay_threshold = c4_delay_threshold(c4_state->rtt_min);
    path_x->cwin = c4_state->nominal_cwin;
    c4_era_reset(path_x->cnx, path_x, c4_state, current_time);
}

void c4_reset(c4_state_t* c4_state, picoquic_path_t* path_x, uint64_t current_time)
{
    memset(c4_state, 0, sizeof(c4_state_t));
    c4_state->rtt_min = path_x->smoothed_rtt;
    c4_state->rolling_rtt_min = c4_state->rtt_min;
    c4_state->nominal_cwin = PICOQUIC_CWIN_INITIAL;
    c4_enter_initial(path_x->cnx, path_x, c4_state, current_time);
}

void c4_seed_cwin(c4_state_t* c4_state, picoquic_path_t* path_x, uint64_t bytes_in_flight)
{
    if (c4_state->alg_state == c4_initial) {
        if (path_x->cwin < bytes_in_flight) {
            path_x->cwin = bytes_in_flight;
        }
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
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    int is_congested,
    int is_delay,
    int is_timeout,
    uint64_t current_time)
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
    path_x->cwin = MULT1024(C4_ALPHA_RECOVER_1024, c4_state->nominal_cwin);
    c4_era_reset(cnx, path_x, c4_state, current_time);
    c4_state->alg_state = c4_recovery;
}

/* Enter cruise.
* CWIN is set C4_ALPHA_CRUISE of nominal value (98%?)
* Ack target if set to nominal cwin times log2 of cwin.
*/
static void c4_enter_cruise(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_era_reset(cnx, path_x, c4_state, current_time);
    c4_state->cruise_bytes_ack = 0;
    /* TODO-SHARING: use the x*log(x) formula. */
#if 1
    c4_state->cruise_bytes_target = c4_cruise_bytes_target(c4_state->nominal_cwin);
#else
    c4_state->cruise_bytes_target = 5 * c4_state->nominal_cwin;
#endif
    path_x->cwin = MULT1024(C4_ALPHA_CRUISE_1024, c4_state->nominal_cwin);
    c4_state->alg_state = c4_cruising;
}

/* Enter push.
* CWIN is set C4_ALPHA_PUSH of nominal value (125%?)
* Ack target if set to nominal cwin times log2 of cwin.
*/
static void c4_enter_push(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_state->nb_push_no_congestion++;
    path_x->cwin = MULT1024(C4_ALPHA_PUSH_1024, c4_state->nominal_cwin);
    if (path_x->cwin < c4_state->nominal_cwin + path_x->send_mtu) {
        path_x->cwin = c4_state->nominal_cwin + path_x->send_mtu;
    }
    c4_era_reset(cnx, path_x, c4_state, current_time);
    c4_state->alg_state = c4_pushing;
}

uint64_t c4_compute_corrected_era_bytes(c4_state_t* c4_state, uint64_t current_time)
{
    uint64_t era_bytes = c4_state->era_bytes_ack;
    uint64_t era_duration = current_time - c4_state->era_time_stamp;
    uint64_t duration_max = MULT1024(1024 + 102, c4_state->rtt_min);

    if (c4_state->rtt_min_is_trusted && era_duration > duration_max) {
        uint64_t ratio_1024 = (duration_max * 1024) / era_duration;
        era_bytes = MULT1024(ratio_1024, era_bytes);
    }
    return era_bytes;
}

/* Handle data ack event.
 */
void c4_handle_ack(picoquic_cnx_t* cnx, picoquic_path_t* path_x, c4_state_t* c4_state, uint64_t current_time, uint64_t nb_bytes_acknowledged)
{
    c4_state->era_bytes_ack += nb_bytes_acknowledged;
    c4_state->cruise_bytes_ack += nb_bytes_acknowledged;
    if (c4_state->alg_state == c4_initial) {
        c4_state->nominal_cwin += nb_bytes_acknowledged;
        path_x->cwin = c4_state->nominal_cwin;
        /* TODO-LIMITED there should be a limit, not increasing if already larger than bytes in flight. */
    }

    if (c4_era_check(cnx, path_x, c4_state)) {
        if (c4_state->era_bytes_ack > c4_state->nominal_cwin) {
            /* This should only happen in the era that follows a pushing attempt. */
            uint64_t corrected_era_bytes = c4_compute_corrected_era_bytes(c4_state, current_time);
            if (corrected_era_bytes > c4_state->nominal_cwin) {
                c4_state->nominal_cwin = corrected_era_bytes;
                path_x->cwin = c4_state->nominal_cwin;
            }
        }

        /* we could set a bandwidth estimate, but we would rather use the main code's estimate */

        switch (c4_state->alg_state) {
        case c4_recovery:
            c4_state->nb_cc_events = 0;
            c4_enter_cruise(cnx, path_x, c4_state, current_time);
            break;
        case c4_cruising:
            c4_era_reset(cnx, path_x, c4_state, current_time);
            break;
        case c4_pushing:
            c4_enter_recovery(cnx, path_x, c4_state, 0, 0, 0, current_time);
            break;
        default:
            c4_era_reset(cnx, path_x, c4_state, current_time);
            break;
        }
    }

    if (c4_state->alg_state == c4_cruising){
        if (c4_state->nb_push_no_congestion >= C4_NB_PUSH_BEFORE_RESET) {
            c4_enter_initial(cnx, path_x, c4_state, current_time);
        }
        else if(c4_state->cruise_bytes_ack >= c4_state->cruise_bytes_target) {
            c4_enter_push(cnx, path_x, c4_state, current_time);
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
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time,
    int is_delay,
    int is_timeout)
{
    c4_state->nb_push_no_congestion = 0;

    if (c4_state->alg_state == c4_recovery &&
        (!is_timeout || !c4_state->last_freeze_was_timeout) &&
        (!is_delay || !c4_state->last_freeze_was_not_delay)) {
        /* Do not treat additional events during same freeze interval */
        return;
    }

    c4_state->nb_cc_events = 0;

    if (is_delay && c4_state->alg_state != c4_initial) {
        c4_state->nominal_cwin -= MULT1024(C4_BETA_1024, c4_state->nominal_cwin);
    }
    else {
        c4_state->nominal_cwin -= MULT1024(C4_BETA_LOSS_1024, c4_state->nominal_cwin);
    }

    if (is_timeout || c4_state->nominal_cwin < PICOQUIC_CWIN_MINIMUM) {
        c4_state->nominal_cwin = PICOQUIC_CWIN_MINIMUM;
    }

    c4_enter_recovery(cnx, path_x, c4_state, 1, is_delay, is_timeout, current_time);

    picoquic_update_pacing_data(cnx, path_x, 0);

    path_x->is_ssthresh_initialized = 1;
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
        /* TODO: commented out code below will trigger either "initial" or "pushing" depending 
        * whether recovery was entered on timeout or other wise. Move to "exit recovery".
        */
#if 0
        if (c4_state->alg_state == c4_recovery && 
            (current_time > c4_state->end_of_freeze ||
                c4_state->era_sequence <= picoquic_cc_get_ack_number(cnx, path_x))) {
            if (c4_state->last_freeze_was_timeout) {
                c4_state->alg_state = c4_initial;
            }
            else {
                c4_state->alg_state = c4_pushing;
            }
            c4_state->last_freeze_was_not_delay = 0;
            c4_state->last_freeze_was_timeout = 0;

            c4_state->nb_cc_events = 0;
            c4_state->era_bytes_ack = 0;
        }
#endif

        switch (notification) {
        case picoquic_congestion_notification_acknowledgement:
            /* Handle path transition, etc. */
            c4_handle_ack(cnx, path_x, c4_state, current_time, ack_state->nb_bytes_acknowledged);
            /* Compute pacing data. */
            picoquic_update_pacing_data(cnx, path_x, 0);
            break;
        case picoquic_congestion_notification_ecn_ec:
            /* TODO: ECN is special? Implement the prague logic */
            c4_notify_congestion(cnx, path_x, c4_state, current_time, 0, 0);
            break;
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_timeout:
            if (picoquic_cc_hystart_loss_test(&c4_state->rtt_filter, notification, ack_state->lost_packet_number, PICOQUIC_SMOOTHED_LOSS_THRESHOLD)) {
                c4_notify_congestion(cnx, path_x, c4_state, current_time, 0,
                    (notification == picoquic_congestion_notification_timeout) ? 1 : 0);
            }
            break;
        case picoquic_congestion_notification_spurious_repeat:
            if (c4_state->nb_cc_events > 0) {
                c4_state->nb_cc_events--;
            }
            break;
        case picoquic_congestion_notification_rtt_measurement:
        {
            uint64_t delta_rtt = 0;

            picoquic_cc_filter_rtt_min_max(&c4_state->rtt_filter, ack_state->rtt_measurement);

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
            if (ack_state->rtt_measurement < c4_state->rtt_min) {
                c4_state->delay_threshold = c4_delay_threshold(c4_state->rtt_min);
            }
            else if (c4_state->rtt_min_is_trusted){
                delta_rtt = ack_state->rtt_measurement - c4_state->rtt_min;
            }
            else {
                c4_state->rtt_min = ack_state->rtt_measurement; 
                c4_state->rolling_rtt_min = ack_state->rtt_measurement;
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
                    c4_notify_congestion(cnx, path_x, c4_state, current_time, 1, 0);
                }
            }
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
