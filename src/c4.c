/*
* Author: Christian Huitema
* Copyright (c) 2025, Private Octopus, Inc.
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
* Principles were later revised to track data rate and max RTT.
 */

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
* Slowdown.  Periodic slowdown to 1/2 the nominal CWIN, in order to reset
*            the min delay.
* Checking:  Post slowdown. use nominal CWND until the min CWND is
*            verified.
* 
* Modes:
* pig_war: If we detect the need to compete with Cubic. In that mode, stop
*           treating delay variations as congestion signals.
* 
* Transitions:
*            initial to recovery -- similar to hystart for now.
*            recovery to initial -- if measurements show increase in data rate compared to era.
*            recovery to cruising -- at the end of period.
*            cruising, pushing to recovery -- if excess delay, loss or ECN
*            pushing to recovery -- at end of period.
*            to slowdown..
* 
* 
* State variables:
* - CWIN. Main control variable.
* - Sequence number of first packet sent in epoch. Epoch ends when this is acknowledged.
* - Observed data rate. Measured at the end of epoch N, reflects setting at epoch N-1.
* - Average rate of EC marking
* - Average rate of Packet loss
* - Average rate of excess delay
* - Number of cruising bytes sent.
* - Cruising bytes target before transition to push
* - RTT min
 */


#define PICOQUIC_CC_ALGO_NUMBER_C4 8
#define C4_DELAY_THRESHOLD_MAX 25000
#define MULT1024(c, v) (((v)*(c)) >> 10)
#define C4_ALPHA_NEUTRAL_1024 1024 /* 100% */
#define C4_ALPHA_RECOVER_1024 960 /* 93.75% */
#define C4_ALPHA_CRUISE_1024 1024 /* 100% */
#define C4_ALPHA_PUSH_1024 1280 /* 125 % */
#define C4_ALPHA_PUSH_LOW_1024 1088 /* 106.25 % */
#define C4_ALPHA_INITIAL 2048 /* 200% */
#define C4_ALPHA_SLOWDOWN_1024 512 /* 50% */
#define C4_ALPHA_CHECKING_1024 1024 /* 100% */
#define C4_ALPHA_PREVIOUS_LOW 960 /* 93.75% */
#define C4_BETA_1024 128 /* 0.125 */
#define C4_BETA_LOSS_1024 256 /* 25%, 1/4th */
#define C4_BETA_INITIAL_1024 512 /* 50% */
#define C4_NB_PACKETS_BEFORE_LOSS 20
#define C4_NB_PUSH_BEFORE_RESET 4
#define C4_NB_CRUISE_BEFORE_PUSH 4
#define C4_MAX_DELAY_ERA_CONGESTIONS 4
#define C4_SLOWDOWN_DELAY 5000000 /* target seconds after last min RTT validation and slow down */
#define C4_SLOWDOWN_RTT_COUNT 10 /* slowdown delay must be at least 10 RTT */
#define C4_RTT_MARGIN_5PERCENT 51 

typedef enum {
    c4_initial = 0,
    c4_recovery,
    c4_cruising,
    c4_pushing,
    c4_slowdown,
    c4_checking
} c4_alg_state_t;

typedef struct st_c4_state_t {
    c4_alg_state_t alg_state;
    uint64_t nominal_cwin; /* Control variable if CWIN based. */
    uint64_t nominal_rate; /* Control variable if not delay based. */
    uint64_t alpha_1024_current;
    uint64_t alpha_1024_previous;
    uint64_t nominal_max_rtt;
    uint64_t nb_packets_in_startup;
    uint64_t era_sequence; /* sequence number of first packet in era */
    uint64_t nb_cruise_left_before_push; /* Number of cruise periods required before push */
    uint64_t seed_cwin; /* Value of CWIN remembered from previous trials */
    uint64_t max_rate; /* max nominal rate since initial */
    uint64_t max_cwin; /* max nominal CWIN since initial */
    uint64_t max_bytes_ack; /* maximum bytes acked since congestion */

    int nb_eras_no_increase;
    int nb_push_no_congestion; /* Number of successive pushes with no congestion */
    int nb_eras_delay_based_decrease; /* Number of successive delay based decreases */
    uint64_t push_rate_old;
    uint64_t push_alpha;

    uint64_t rtt_min;
    uint64_t rtt_min_stamp;
    uint64_t running_rtt_min;
    uint64_t era_max_rtt;
    uint64_t last_slowdown_rtt_min; /* Handling of slowdown */

    uint64_t delay_threshold;
    uint64_t recent_delay_excess;
    int nb_rtt_update_since_discovery;

    unsigned int last_freeze_was_not_delay : 1;
    unsigned int rtt_min_is_trusted : 1;
    unsigned int congestion_notified : 1;
    unsigned int congestion_delay_notified : 1;
    unsigned int push_was_not_limited : 1;
    unsigned int pig_war : 1;
    unsigned int use_seed_cwin : 1;
    unsigned int do_cascade : 1;
    unsigned int do_slow_push : 1;



    picoquic_min_max_rtt_t rtt_filter;
    /* Handling of options. */
    char const* option_string;
} c4_state_t;

static void c4_enter_recovery(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    int is_congested,
    int is_delay,
    uint64_t current_time);
static void c4_enter_cruise(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time);

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

/* On an ACK event, compute the corrected value of the number of bytes delivered */
static uint64_t c4_compute_corrected_delivered_bytes(c4_state_t* c4_state, uint64_t nb_bytes_delivered, uint64_t rtt_measurement, uint64_t current_time)
{
    uint64_t duration_max = MULT1024(1024 + 51, c4_state->rtt_min);

    if (rtt_measurement > duration_max) {
        uint64_t ratio_1024 = (duration_max * 1024) / rtt_measurement;
        nb_bytes_delivered = MULT1024(ratio_1024, nb_bytes_delivered);
    }

    return nb_bytes_delivered;
}

/*
* c4_apply_rate_and_cwin:
* Manage all setting of the actual cwin, pacing rate and quantum
* at a single place, based on the state parameters computed
* in the other functions.
*/

static void c4_apply_rate_and_cwin(
    picoquic_path_t* path_x,
    c4_state_t* c4_state)
{
    uint64_t target_cwin = MULT1024(c4_state->alpha_1024_current, c4_state->nominal_cwin);
    uint64_t pacing_rate = MULT1024(c4_state->alpha_1024_current, c4_state->nominal_rate);
    uint64_t quantum;

    if (c4_state->alg_state == c4_initial) {
        /* Initial special case: bandwidth discovery, and seed cwin */
        if (c4_state->nb_packets_in_startup > 0) {

            uint64_t min_win = (path_x->peak_bandwidth_estimate * path_x->smoothed_rtt / 1000000) / 2;
            if (min_win > target_cwin) {
                target_cwin = min_win;
            }
            if (path_x->peak_bandwidth_estimate > 2*pacing_rate) {
                pacing_rate = path_x->peak_bandwidth_estimate / 2;
            }
        }
        if (c4_state->use_seed_cwin && c4_state->seed_cwin > target_cwin) {
            uint64_t target_rate;
            /* Match half the difference between seed and computed CWIN */
            target_cwin = (c4_state->seed_cwin + target_cwin) / 2;
            target_rate = (c4_state->seed_cwin * 1000000) / path_x->smoothed_rtt;
            if (target_rate > pacing_rate) {
                pacing_rate = target_rate;
            }
        }
        /* Increase pacing rate by factor 1.25 to allow for bunching of packets */
        pacing_rate = MULT1024(1024+256, pacing_rate);
    }
    if (c4_state->pig_war || c4_state->nominal_cwin < c4_state->nominal_max_rtt){
        /* In the pig war state, we need to loosen the congestion window
         * in order to continue sending through jitter events. We do that by
         * adding half of the difference between the nominal window and the
         * max window acknowledged so far. See design spec for details. */
        uint64_t jitter_cwin = (pacing_rate * c4_state->nominal_max_rtt)/1000000;
        if (jitter_cwin > target_cwin) {
            target_cwin = jitter_cwin;
        }
    }

    if (c4_state->alg_state == c4_pushing) {
        if (target_cwin < c4_state->nominal_cwin + path_x->send_mtu) {
            target_cwin = c4_state->nominal_cwin + path_x->send_mtu;
        }
    }

    path_x->cwin = target_cwin;
    quantum = target_cwin / 4;
    if (quantum > 0x10000) {
        quantum = 0x10000;
    }
    else if (quantum < 2 * path_x->send_mtu) {
        quantum = 2 * path_x->send_mtu;
    }
    picoquic_update_pacing_rate(path_x->cnx, path_x, (double)pacing_rate, quantum);
}

/* Perform evaluation. Assess whether the previous era resulted
 * in a significant increase or not.
 */
static void c4_growth_evaluate(c4_state_t* c4_state)
{
    int is_growing = 0;
    if (c4_state->push_alpha > C4_ALPHA_PUSH_LOW_1024) {
        /* If the value of "push_alpha" was large enough, we can reasonably
         * measure growth. */
        uint64_t target_rate = (3*c4_state->push_rate_old +
            MULT1024(c4_state->push_alpha, c4_state->push_rate_old)) / 4;
        is_growing = (c4_state->nominal_rate > target_rate);
    }
    else {
        /* If the value was not big enough, we have to make decision
         * based on congestion signals.
         */
        is_growing = (c4_state->nominal_rate > c4_state->push_rate_old &&
            !c4_state->congestion_notified);
    }
    if (is_growing) {
        c4_state->nb_push_no_congestion++;
        c4_state->nb_eras_no_increase = 0;
        if (c4_state->nb_eras_delay_based_decrease > 0) {
            c4_state->nb_eras_delay_based_decrease--;
        }
    }
    else if (c4_state->push_was_not_limited) {
        c4_state->nb_push_no_congestion = 0;
        c4_state->nb_eras_no_increase++;
        if (c4_state->congestion_delay_notified) {
            c4_state->nb_eras_delay_based_decrease++;
        }
    }
}

static void c4_growth_reset(c4_state_t* c4_state)
{
    c4_state->congestion_notified = 0;
    c4_state->congestion_delay_notified = 0;
    c4_state->push_was_not_limited = 0;
    c4_state->push_rate_old = c4_state->nominal_rate;
    /* Push alpha will have to be reset to the correct value when entering push */
    c4_state->push_alpha = c4_state->alpha_1024_current;
}


/* End of round trip.
* Happens if packet waited for is acked.
* Add bandwidth measurement to bandwidth barrel.
 */
static int c4_era_check(
    picoquic_path_t* path_x,
    c4_state_t* c4_state)
{
    if (path_x->cnx->cnx_state < picoquic_state_ready) {
        return 0;
    }
    else {
        return (picoquic_cc_get_ack_number(path_x->cnx, path_x) >= c4_state->era_sequence);
    }
}

static void c4_era_reset(
    picoquic_path_t* path_x,
    c4_state_t* c4_state)
{
    c4_state->era_sequence = picoquic_cc_get_sequence_number(path_x->cnx, path_x);
    c4_state->era_max_rtt = 0;
    c4_state->alpha_1024_previous = c4_state->alpha_1024_current;
}

static void c4_enter_initial(picoquic_path_t* path_x, c4_state_t* c4_state, uint64_t current_time)
{
    c4_state->alg_state = c4_initial;
    c4_state->nb_push_no_congestion = 0;
    c4_state->alpha_1024_current = C4_ALPHA_INITIAL;
    c4_state->nb_packets_in_startup = 0;
    c4_state->nb_rtt_update_since_discovery = 0;
    c4_era_reset(path_x, c4_state);
    c4_state->nb_eras_no_increase = 0;
    c4_state->nb_eras_delay_based_decrease = 0;
    c4_state->max_cwin = 0;
    c4_state->max_rate = 0;
    c4_growth_reset(c4_state);
}

static void c4_set_options(c4_state_t* c4_state)
{
    if (c4_state->option_string != NULL) {
        char const* x = c4_state->option_string;
        char c;
        int ended = 0;

        while ((c = *x) != 0 && !ended) {
            x++;
            switch (c) {
            case 'K': /* allow the cascade behavior */
                c4_state->do_cascade = 1;
                break;
            case 'k': /* disallow the cascade behavior */
                c4_state->do_cascade = 0;
                break;
            case 'O': /* allow the slow push behavior */
                c4_state->do_slow_push = 1;
                break;
            case 'o': /* disallow the slow push behavior */
                c4_state->do_slow_push = 0;
                break;
            default:
                ended = 1;
                break;
            }
        }
    }
}

void c4_reset(c4_state_t* c4_state, picoquic_path_t* path_x, char const* option_string, uint64_t current_time)
{
    memset(c4_state, 0, sizeof(c4_state_t));
    c4_state->option_string = option_string;
    c4_state->rtt_min = UINT64_MAX;
    c4_state->nominal_cwin = PICOQUIC_CWIN_INITIAL/2;
    c4_state->alpha_1024_current = C4_ALPHA_INITIAL;
    c4_state->do_slow_push = 1;
    c4_state->do_cascade = 1;
    c4_state->last_slowdown_rtt_min = 0;
    c4_set_options(c4_state);
    c4_enter_initial(path_x, c4_state, current_time);
}

void c4_seed_cwin(c4_state_t* c4_state, picoquic_path_t* path_x, uint64_t bytes_in_flight)
{
    if (c4_state->alg_state == c4_initial) {
        c4_state->use_seed_cwin = 1;
        c4_state->seed_cwin = bytes_in_flight;
    }
}

static void c4_exit_initial(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_congestion_notification_t notification, uint64_t current_time)
{
    /* We assume that any required correction is done prior to calling this */
    int is_congested = 0;

    c4_state->nb_eras_no_increase = 0;
    c4_state->nb_push_no_congestion = 0;
    c4_state->nb_eras_delay_based_decrease = 0;
    c4_enter_recovery(path_x, c4_state, is_congested, 0, current_time);
}

static void c4_initial_handle_rtt(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_congestion_notification_t notification, uint64_t rtt_measurement, uint64_t current_time)
{
    /* HyStart. */
    /* Using RTT increases as congestion signal. This is used
     * for getting out of slow start, but also for ending a cycle
     * during congestion avoidance */
    /* we do not directly use "hystart test", because we want to separate the
    * "update_rtt" functions from the actual tests.
     */

    if (c4_state->rtt_filter.is_init && c4_state->recent_delay_excess > 0
        && c4_state->nb_eras_no_increase > 1){

        c4_exit_initial(path_x, c4_state, notification, current_time);
    }
}

static void c4_initial_handle_loss(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_congestion_notification_t notification, uint64_t current_time)
{
    c4_state->nb_packets_in_startup += 1;
    if (c4_state->nb_packets_in_startup > C4_NB_PACKETS_BEFORE_LOSS) {
        c4_exit_initial(path_x, c4_state, notification, current_time);
    }
}

static void c4_initial_handle_ack(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    c4_state->nb_packets_in_startup += 1;
    if (c4_state->use_seed_cwin && c4_state->nominal_cwin >= c4_state->seed_cwin) {
        /* The nominal bandwidth is larger than the seed. The seed has been validated. */
        c4_state->use_seed_cwin = 0;
    }
    if (c4_era_check(path_x, c4_state)) {
        /*
        * We should only consider a lack of increase if the application is
        * not app limited. However, if the application *is* app limited,
        * that strategy leads to staying in "initial" mode forever,
        * which is not good either. If we don't check if the app limited,
        * we lose in the very common case where the server sends almost
        * nothing for several RTT, until the client asks for some data.
        * So we test that we have seen at least some data.
        */
        c4_growth_evaluate(c4_state);
        c4_era_reset(path_x, c4_state);
        if (c4_state->nb_eras_no_increase >= 3) {
            c4_exit_initial(path_x, c4_state, picoquic_congestion_notification_acknowledgement, current_time);
            return;
        }
        else {
            c4_growth_reset(c4_state);
        }
    }
}

void c4_init(picoquic_cnx_t * cnx, picoquic_path_t* path_x, char const* option_string, uint64_t current_time)
{
    /* Initialize the state of the congestion control algorithm */
    c4_state_t* c4_state = path_x->congestion_alg_state;
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(cnx);
#endif
    
    if (c4_state == NULL) {
        c4_state = (c4_state_t*)malloc(sizeof(c4_state_t));
    }
    
    if (c4_state != NULL){
        cnx->is_lost_feedback_notification_required = 1;
        
        c4_reset(c4_state, path_x, option_string, current_time);
    }

    path_x->congestion_alg_state = (void*)c4_state;
}

/* Reset RTT filter.
* We are reusing the hystart code for its low pass rtt filter, but in some
* cases the filter keeps more memory than required, so it is better
* to reset it.
 */
static void c4_reset_rtt_filter(c4_state_t* c4_state)
{
    for (int i = 0; i < PICOQUIC_MIN_MAX_RTT_SCOPE; i++) {
        picoquic_cc_filter_rtt_min_max(&c4_state->rtt_filter, c4_state->rtt_min);
    }
    c4_state->rtt_filter.rtt_filtered_min = c4_state->rtt_min;
}

/*
* pig_war_log(): collection of "starting pig war" statistics.
* This code collect statistics on the pig war detection.
* It was used to check the "spurious" detection in prior
* versions of the algorithm, collecting the state of key
* variables at the time of the detection. This enabled a
* better tuning of the algorithm. It could be reused if
* we need to tune it again.
* 
* The code writes a file per trial in the
* "pwl" (pig war log) folder in the current directory. The
* file name is obtained by combining the initial connection
* id with a random number to differentiate between
* multiple runs of the same simulation.
* 
* By default, this code is not compiled.
*/
/* #define PIG_WAR_STATS */
#ifdef PIG_WAR_STATS

typedef enum {
    pig_war_none = 0,
    pig_war_start = 1,
    pig_war_slowdown = 2,
    pig_war_chaotic = 3,
    pig_war_checking = 4
} pig_war_stat_entry;

static void pig_war_log(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    pig_war_stat_entry pwe,
    uint64_t current_time)
{
    static uint32_t rd_id = 0;
    if (rd_id == 0 && !path_x->cnx->client_mode) {
        char file_name[256];
        size_t written = 0;
        FILE* F;

        file_name[0] = 'p';
        file_name[1] = 'w';
        file_name[2] = 'l';
#ifdef _WINDOWS
        file_name[3] = '\\';
#else
        file_name[3] = '/';
#endif
        for (int i = 0; i < 4; i++) {
            (void)picoquic_sprintf(&file_name[4 + 2 * i], 252 - (2 * i), &written, "%02x", path_x->cnx->initial_cnxid.id[i]);
        }
        rd_id = (uint32_t)picoquic_uniform_random(100000);
        (void)picoquic_sprintf(&file_name[12], 244, &written, ".%u.txt", rd_id);
        if ((F = picoquic_file_open(file_name, "wt")) != NULL) {
            int pig_war = c4_state->pig_war;
            if (pwe == pig_war_start || pwe == pig_war_checking) {
                pig_war = 1;
            }
            (void)fprintf(F, "%" PRIu64 ", %d, %" PRIu64", %" PRIu64", %" PRIu64", %" PRIu64", %" PRIu64", %"
                          PRIu64", %" PRIu64", %" PRIu64", %" PRIu64", %d, %d\n",
                current_time, c4_state->nb_eras_delay_based_decrease, c4_state->nominal_cwin, c4_state->nominal_rate,
                c4_state->max_cwin, c4_state->max_rate,
                c4_state->rtt_min, c4_state->rtt_filter.sample_min,
                c4_state->rtt_filter.sample_max, path_x->rtt_variant, path_x->smoothed_rtt,
                pig_war, pwe);
            picoquic_file_close(F);
        }
    }
}
#endif

/*
* Start a pig war. We detect that there this connection is sharing
* the bottleneck with an uncooperating other connection that does
* not back off when delays increase by observing a number of
* successive congestion notifications caused by "excess delay".
* In that case, we enter "pig war" mode: reset the min RTT
* to the current RTT value, re-enter slow start, and after that
* ignore "excess delay" signals until further notice.
 */
static void c4_start_pig_war(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_state->pig_war = 1;
    c4_state->nb_eras_delay_based_decrease = 0;
    c4_state->rtt_min = path_x->rtt_sample;
    c4_state->rtt_min_stamp = current_time;
    c4_reset_rtt_filter(c4_state);
    c4_state->rtt_filter.rtt_filtered_min = path_x->rtt_sample;
    c4_enter_initial(path_x, c4_state, current_time);
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
    uint64_t current_time)
{
    if (!is_congested) {
        c4_state->last_freeze_was_not_delay = 0;
    }
    else {
        c4_state->nb_push_no_congestion = 0;
        c4_state->last_freeze_was_not_delay = !is_delay;
    }
    c4_state->alpha_1024_current = C4_ALPHA_RECOVER_1024;

    if (c4_state->alg_state == c4_initial) {
        c4_growth_reset(c4_state);
    }
    c4_state->alg_state = c4_recovery;
    c4_era_reset(path_x, c4_state);
}

/* Exit recovery. We will test whether the previous push was successful.
* We do that by comparing the nominal cwin to the value before entering
* push. This "previous value" would be zero if the previous state
* was not pushing.
 */

static void c4_exit_recovery(
    picoquic_path_t* path_x,
    c4_state_t* c4_state, uint64_t current_time)
{
    c4_growth_evaluate(c4_state);
    c4_growth_reset(c4_state);
    if (c4_state->nominal_cwin > c4_state->max_cwin) {
        c4_state->max_cwin = c4_state->nominal_cwin;
    }
    if (c4_state->nominal_rate > c4_state->max_rate) {
        c4_state->max_rate = c4_state->nominal_rate;
    }
    c4_state->recent_delay_excess = 0;
    c4_state->nb_rtt_update_since_discovery = 0;
    /* Check whether we have too many delay based events, as this
    * is indivative of competition with non cooperating connections.
    */
    if (!c4_state->pig_war &&
        ((c4_state->nb_eras_delay_based_decrease >= C4_MAX_DELAY_ERA_CONGESTIONS &&
        2*c4_state->nominal_cwin < c4_state->max_cwin) ||
        (c4_state->nb_eras_delay_based_decrease > C4_MAX_DELAY_ERA_CONGESTIONS &&
            5 * c4_state->nominal_cwin < 4*c4_state->max_cwin))) {
#ifdef PIG_WAR_STATS
        pig_war_log(path_x, c4_state, pig_war_start, current_time);
#endif
        picoquic_log_app_message(path_x->cnx, "C4-Start pig war on decrease, chaotic: %d", 0);
        c4_start_pig_war(path_x, c4_state, current_time);
    }
    else if (c4_state->nb_push_no_congestion >= C4_NB_PUSH_BEFORE_RESET) {
        if (c4_state->pig_war) {
            /* End the pig war here. Bandwidth has increased, which means the  competing
             * connection is probably gone. */
            picoquic_log_app_message(path_x->cnx, "C4-Stop pig war, chaotic: %d", 0);
            c4_state->pig_war = 0;
            c4_state->nb_push_no_congestion = 0;
        }
        else {
            c4_enter_initial(path_x, c4_state, current_time);
        }
    }
    else if (c4_state->pig_war && c4_state->nb_push_no_congestion > 0) {
        /* End the pig war here. Bandwidth has increased, which means the competing
         * connection is probably gone. */
        picoquic_log_app_message(path_x->cnx, "C4-Stop pig war early, chaotic: %d", 0);
        c4_state->pig_war = 0;
        c4_state->nb_push_no_congestion = 0;
    }
    else {
#ifdef PIG_WAR_STATS
        if (current_time > 4000000 && !c4_state->pig_war) {
            pig_war_log(path_x, c4_state, pig_war_none, current_time);
        }
#endif
        c4_enter_cruise(path_x, c4_state, current_time);
    }
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
    c4_state->use_seed_cwin = 0;

    if (c4_state->nb_push_no_congestion > 0 && c4_state->do_cascade) {
        c4_state->nb_cruise_left_before_push = 0;
    }
    else {
        c4_state->nb_cruise_left_before_push = C4_NB_CRUISE_BEFORE_PUSH;
    }
    c4_state->alpha_1024_current = C4_ALPHA_CRUISE_1024;
    c4_state->alg_state = c4_cruising;
}

/* Enter push.
* CWIN is set C4_ALPHA_PUSH of nominal value (125%?)q
* Ack target if set to nominal cwin times log2 of cwin.
*/
static void c4_enter_push(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    if (c4_state->nb_push_no_congestion == 0 && !c4_state->pig_war && c4_state->do_slow_push) {
        /* If the previous push was not successful, increase by 6.25% instead of 25% */
        c4_state->alpha_1024_current = C4_ALPHA_PUSH_LOW_1024;
    }
    else {
        c4_state->alpha_1024_current = C4_ALPHA_PUSH_1024;
    }
    c4_state->push_alpha = c4_state->alpha_1024_current;
    c4_era_reset(path_x, c4_state);
    c4_state->alg_state = c4_pushing;
}


/* Reset the min RTT and the associated tracking variables.
 */
static void c4_reset_min_rtt(
    c4_state_t* c4_state,
    uint64_t new_rtt_min,
    uint64_t last_rtt,
    uint64_t current_time)
{
    c4_state->rtt_min = new_rtt_min;
    c4_state->running_rtt_min = last_rtt;
    c4_state->rtt_min_stamp = current_time;
    c4_state->delay_threshold = c4_delay_threshold(c4_state->rtt_min);
    c4_state->rtt_min_is_trusted = 1;
}

/* Handling of slowdown transitions
* Enter slowdown if the rtt_min_stamp is more than 5 seconds old.
* On era exit, check whether this is the first of two consecutive
* eras. If yes, simply restart another slowdown era. Else,
* we check whether this is the second slow down period since the
* last reset: if it is, we can override the current min_rtt with
* the recent minimum.
* When we do that, we know that the second slowdown flight has
* not been acknowledged. We may receive new acks and new RTT
* measurement. If they carry a low value than the min RTT, the
* value will be updated.
*/

/*
* Upon entering slowdown, we should reduce the sending rate. However, if
* the current RTT exceeds the sending rate, we are already operating at
* a reduced rate, and reducing it too much leads to stalling. In this code,
* the new CWIN is computed at the minimum of half the estimated BDP
* and the nominal CWIN. */
void c4_enter_slowdown(picoquic_path_t* path_x, c4_state_t* c4_state, uint64_t current_time)
{
    uint64_t current_rtt = c4_state->rtt_filter.sample_max;
    c4_state->alpha_1024_current = C4_ALPHA_SLOWDOWN_1024;
    c4_reset_min_rtt(c4_state, c4_state->rtt_min, current_rtt, current_time);
    c4_state->alg_state = c4_slowdown;
    c4_era_reset(path_x, c4_state);
}

int c4_is_slowdown_needed(c4_state_t* c4_state, uint64_t current_time, uint64_t bytes_in_past_flight, int * is_natural)
{
    int ret = 0;

    if (c4_state->alg_state != c4_slowdown && c4_state->alg_state != c4_checking) {
        uint64_t slowdown_delay = C4_SLOWDOWN_DELAY;
        uint64_t cwnd_target = c4_state->nominal_cwin;
        int is_urgent = 0;

        if (slowdown_delay < c4_state->rtt_min * C4_SLOWDOWN_RTT_COUNT) {
            slowdown_delay = c4_state->rtt_min * C4_SLOWDOWN_RTT_COUNT;
        }

        if (c4_state->rtt_filter.sample_min > c4_state->rtt_min) {
            uint64_t alpha_delay = c4_state->rtt_min * 1024 / c4_state->rtt_filter.sample_min;
            uint64_t alpha_cwnd = 1024 * c4_state->rtt_filter.sample_min / c4_state->rtt_min;
            cwnd_target = MULT1024(alpha_cwnd, c4_state->nominal_cwin);
            slowdown_delay = MULT1024(alpha_delay, slowdown_delay);
            is_urgent = 1;
        }

        *is_natural = (2 * bytes_in_past_flight < cwnd_target);

        if ((*is_natural && is_urgent) || (c4_state->rtt_min_stamp + slowdown_delay < current_time)){
            ret = 1;
        }
    }
    return ret;
}

/* Enter checking state 
* Compare the era RTT and the previous slowdown RTT to
* the previous value of min RTT. If higher, reset,
* otherwise enter cruise.
*/

static void c4_enter_checking(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    c4_state->alpha_1024_current = C4_ALPHA_CHECKING_1024;
    c4_state->alg_state = c4_checking;
    c4_era_reset(path_x, c4_state);
}

/* End of checking era.
* Compare the era RTT and the previous slowdown RTT to
* the previous value of min RTT. If higher, reset,
* otherwise enter cruise.
* 
* When enter this function, the "running rtt min" represents the min RTT
* observed since the last call to "c4_reset_min_rtt", which could be
* either:
* - the last time we found an RTT min lower than the current value,
* - or, the last call to `c4_end_checking_era`.
* The variable `last_slowdown_rtt_min` contains the value of the
* running minimum at the end of the previous slowdown, or 0 if no
* slowdown was performed yet.
* 
* If both "running rtt min" and "last_slowdown_rtt_min" are larger
* than "rtt_min", we meet the condition of "two clean observations
* that rtt_min as changed, and we reset the connection, discovering
* new values of rtt_min and windows size.
*/

static void c4_end_checking_era(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time)
{
    uint64_t last_slowdown_rtt_min = c4_state->last_slowdown_rtt_min;
    if (path_x->rtt_sample < c4_state->running_rtt_min) {
        /* A bit of a bug in the picoquic event organization, but the ACK
        * that ends the era can be signaled before the RTT UPDATE. Thus,
        * we consider the last sample here.
         */
        c4_state->running_rtt_min = path_x->rtt_sample;
    }
    c4_state->last_slowdown_rtt_min = c4_state->running_rtt_min;
    if (c4_state->running_rtt_min > c4_state->rtt_min &&
        last_slowdown_rtt_min > c4_state->rtt_min) {
        if (!c4_state->pig_war && path_x->rtt_sample > 2 * c4_state->rtt_min) {
#ifdef PIG_WAR_STATS
            pig_war_log(path_x, c4_state, pig_war_checking, current_time);
#endif
            picoquic_log_app_message(path_x->cnx, "C4-Start pig war on checking, chaotic: %d", 0);
            c4_start_pig_war(path_x, c4_state, current_time);
        }
        else {
#ifdef PIG_WAR_STATS
            pig_war_log(path_x, c4_state, pig_war_slowdown, current_time);
#endif
            c4_state->nb_eras_delay_based_decrease = 0; /* do not get into pig war just after changing RTT */
            c4_reset_min_rtt(c4_state, c4_state->running_rtt_min, path_x->rtt_sample, current_time);
            c4_reset_rtt_filter(c4_state);
            c4_enter_initial(path_x, c4_state, current_time);
        }
    }
    else {
        /* Leave RTT_MIN unchanged, but reset time stamp, running min, then move to cruising. */
        c4_reset_min_rtt(c4_state, c4_state->rtt_min, path_x->rtt_sample, current_time);
        c4_enter_cruise(path_x, c4_state, current_time);
    }
}

/* Handle data ack event.
 */
void c4_handle_ack(picoquic_path_t* path_x, c4_state_t* c4_state, picoquic_per_ack_state_t* ack_state, uint64_t current_time)
{
    uint64_t previous_rate = c4_state->nominal_rate;
    uint64_t rate_measurement = 0;

    uint64_t corrected_delivered_bytes = c4_compute_corrected_delivered_bytes(c4_state, ack_state->nb_bytes_delivered_since_packet_sent, ack_state->rtt_measurement, current_time);

    if (ack_state->rtt_measurement > 0) {
        uint64_t corrected_rtt = ack_state->rtt_measurement;
        if (corrected_rtt < c4_state->rtt_min && c4_state->rtt_min != UINT64_MAX) {
            corrected_rtt = c4_state->rtt_min;
        }
        rate_measurement = (ack_state->nb_bytes_delivered_since_packet_sent * 1000000) / corrected_rtt;

        if (c4_state->alg_state != c4_initial) {
            /* We find some cases where ACK compression causes the rate measurement
             * to return some really weird values. Outside of the initial phase,
             * we know that the sender will never send faster than the push rate.
             * We limit the possible rate increase to that value */
            uint64_t max_rate = MULT1024(C4_ALPHA_PUSH_1024, c4_state->nominal_rate);
            if (rate_measurement > max_rate) {
                rate_measurement = max_rate;
            }
        }

        if (rate_measurement > c4_state->nominal_rate) {
            c4_state->nominal_rate = rate_measurement;
            c4_state->push_was_not_limited = 1;
        }
    }

    if (corrected_delivered_bytes > c4_state->nominal_cwin &&
        (!c4_state->use_seed_cwin || c4_state->alg_state == c4_initial)) {
        c4_state->nominal_cwin = corrected_delivered_bytes;
        c4_state->push_was_not_limited = 1;
    }
    else if (ack_state->nb_bytes_delivered_since_packet_sent > c4_state->nominal_cwin) {
        c4_state->push_was_not_limited = 1;
    }

    if (rate_measurement >= previous_rate &&
        ack_state->nb_bytes_delivered_since_packet_sent > c4_state->max_bytes_ack) {
        c4_state->max_bytes_ack = ack_state->nb_bytes_delivered_since_packet_sent;
    }

    if (c4_state->alg_state == c4_initial) {
        c4_initial_handle_ack(path_x, c4_state, ack_state, current_time);
    }
    else {
        if (c4_era_check(path_x, c4_state)) {
            int is_natural_slowdown = 0;
            /* We use the nominal_max_rtt to estimate the "natural jitter". 
            * This code is only executed if the era ends "naturally" -- if the era
            * was cut short by a congestion event, we do not update the max RTT
             */
            if (path_x->rtt_sample > c4_state->era_max_rtt){
                c4_state->era_max_rtt = path_x->rtt_sample;
            }
            if (c4_state->nominal_max_rtt == 0) {
                c4_state->nominal_max_rtt = c4_state->era_max_rtt;
            }
            else if (c4_state->alpha_1024_previous <= C4_ALPHA_PREVIOUS_LOW) {
                if (c4_state->era_max_rtt >= c4_state->nominal_max_rtt) {
                    c4_state->nominal_max_rtt = c4_state->era_max_rtt;
                }
                else {
                    c4_state->nominal_max_rtt = (7 * c4_state->nominal_max_rtt + c4_state->era_max_rtt) / 8;
                }
            }

            /* Manage the transition to the next state */
            if (c4_is_slowdown_needed(c4_state, current_time,
                ack_state->nb_bytes_delivered_since_packet_sent, &is_natural_slowdown)){

                if (is_natural_slowdown){
                    /* Use the "natural slowdown delay" path */
                    c4_enter_checking(path_x, c4_state, current_time);
                }
                else {
                    /* force an actual slowdown */
                    c4_enter_slowdown(path_x, c4_state, current_time);
                }
            }
            else switch (c4_state->alg_state) {
            case c4_recovery:
                c4_exit_recovery(path_x, c4_state, current_time);
                break;
            case c4_cruising:
                if (c4_state->nb_cruise_left_before_push > 0) {
                    c4_state->nb_cruise_left_before_push--;
                }
                c4_era_reset(path_x, c4_state);
                if (c4_state->nb_cruise_left_before_push <= 0 &&
                    path_x->last_time_acked_data_frame_sent > path_x->last_sender_limited_time) {
                    c4_enter_push(path_x, c4_state, current_time);
                }
                break;
            case c4_pushing:
                c4_enter_recovery(path_x, c4_state, 0, 0, current_time);
                break;
            case c4_slowdown:
                c4_enter_checking(path_x, c4_state, current_time);
                break;
            case c4_checking:
                c4_end_checking_era(path_x, c4_state, current_time);
                break;
            default:
                c4_era_reset(path_x, c4_state);
                break;
            }
        }
    }
}

/* Reaction to ECN/CE or sustained losses.
 * This is more or less the same code as added to bbr.
 * This code is called if an ECN/EC event is received, 
 * or a lost event indicating a high loss rate,
 * or a delay event.
 * 
 * TODO: proper treatment of ECN per L4S
 */
static void c4_notify_congestion(
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t rtt_latest,
    int is_delay,
    uint64_t current_time)
{
    uint64_t beta = C4_BETA_LOSS_1024;

    c4_state->congestion_notified = 1;
    c4_state->congestion_delay_notified |= is_delay;

    if (c4_state->alg_state == c4_recovery &&
        (!is_delay || !c4_state->last_freeze_was_not_delay)) {
        /* Do not treat additional events during same freeze interval */
        return;
    }

    if (is_delay) {
        /* TODO: we should really use bytes in flight! */
        beta = c4_state->recent_delay_excess*1024/c4_state->delay_threshold;

        if (beta > C4_BETA_LOSS_1024) {
            /* capping beta to the standard 1/8th. */
            beta = C4_BETA_LOSS_1024;
        }
    }
    else {
        /* Clear the counter used to filter spurious delay measurements */
        c4_state->recent_delay_excess = 0;
    }

    if (c4_state->alg_state == c4_pushing) {
        c4_state->nb_push_no_congestion = 0;
    }
    else {
        c4_state->nominal_cwin -= MULT1024(beta, c4_state->nominal_cwin);
        c4_state->nominal_rate -= MULT1024(beta, c4_state->nominal_rate);
        c4_state->max_bytes_ack -= MULT1024(beta, c4_state->max_bytes_ack);

        if (c4_state->nominal_cwin < PICOQUIC_CWIN_MINIMUM) {
            c4_state->nominal_cwin = PICOQUIC_CWIN_MINIMUM;
        }
    }

    c4_enter_recovery(path_x, c4_state, 1, is_delay, current_time);

    c4_apply_rate_and_cwin(path_x, c4_state);

    path_x->is_ssthresh_initialized = 1;
}


/* Update RTT:
* Maintain rtt_min, rtt_max, and rtt_min_stamp,
* as well as rtt_min_is_trusted and delay_threshold.
* Do not otherwise change the state.
 */

static void c4_update_rtt(
    c4_state_t* c4_state,
    uint64_t rtt_measurement,
    uint64_t current_time)
{
    picoquic_cc_filter_rtt_min_max(&c4_state->rtt_filter, rtt_measurement);
    c4_state->nb_rtt_update_since_discovery += 1;

    if (c4_state->rtt_filter.rtt_filtered_min == 0 ||
        c4_state->rtt_filter.rtt_filtered_min > c4_state->rtt_filter.sample_max) {
        c4_state->rtt_filter.rtt_filtered_min = c4_state->rtt_filter.sample_max;
    }

    if (c4_state->rtt_filter.is_init) {
        /* We use the maximum of the last samples as the candidate for the
         * min RTT, in order to filter the rtt jitter */
        uint64_t samples_min = c4_state->rtt_filter.sample_max;
        if (2 * c4_state->rtt_filter.sample_min < c4_state->rtt_filter.sample_max) {
            /* The samples themselves have a chaotic behavior. We should
             * not just use the "max of sample" as a threshold, because that leads 
             * to delayed detecting of chaotic jitter. */
            samples_min = (c4_state->rtt_filter.sample_min + c4_state->rtt_filter.sample_max) / 2;
        }
        if (samples_min < c4_state->rtt_min) {
            c4_reset_min_rtt(c4_state, samples_min, rtt_measurement, current_time);
        }

        if (samples_min < c4_state->running_rtt_min) {
            c4_state->running_rtt_min = samples_min;
        }

        if (c4_state->rtt_filter.sample_min > c4_state->rtt_filter.rtt_filtered_min &&
            c4_state->nb_rtt_update_since_discovery > PICOQUIC_MIN_MAX_RTT_SCOPE){
            uint64_t target_rtt = c4_state->nominal_max_rtt + c4_state->delay_threshold;
            if (c4_state->rtt_filter.sample_min > target_rtt) {
                c4_state->recent_delay_excess = c4_state->rtt_filter.sample_min - target_rtt;
            }
        }
        else {
            c4_state->recent_delay_excess = 0;
        }

        if (rtt_measurement > c4_state->era_max_rtt) {
            c4_state->era_max_rtt = rtt_measurement;
        }
    }
}

static void c4_handle_rtt(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t rtt_measurement,
    uint64_t current_time)
{
    if (c4_state->rtt_min_is_trusted && c4_state->recent_delay_excess > 0 /* PICOQUIC_MIN_MAX_RTT_SCOPE */) {
        if (!c4_state->pig_war) {
            /* May well be congested */
            c4_notify_congestion(path_x, c4_state, rtt_measurement, 1, current_time);
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
            c4_apply_rate_and_cwin(path_x, c4_state);
            break;
        case picoquic_congestion_notification_ecn_ec:
            /* TODO: ECN is special? Implement the prague logic */
            if (c4_state->alg_state == c4_initial) {
                c4_initial_handle_loss(path_x, c4_state, notification, current_time);
            }
            else {
                c4_notify_congestion(path_x, c4_state, 0, 0, current_time);
            }
            break;
        case picoquic_congestion_notification_repeat:
            if (c4_state->alg_state == c4_recovery && ack_state->lost_packet_number < c4_state->era_sequence) {
                /* Do not worry about loss of packets sent before entering recovery */
                break;
            }
            if (picoquic_cc_hystart_loss_test(&c4_state->rtt_filter, notification, ack_state->lost_packet_number, PICOQUIC_SMOOTHED_LOSS_THRESHOLD)) {
                if (c4_state->alg_state == c4_initial) {
                    c4_initial_handle_loss(path_x, c4_state, notification, current_time);
                }
                else {
                    c4_notify_congestion(path_x, c4_state, 0, 0, current_time);
                }
            }
            break;
        case picoquic_congestion_notification_timeout:
            /* Treat timeout as PTO: no impact on congestion control */
            break;
        case picoquic_congestion_notification_spurious_repeat:
            /* Remove handling of spurious repeat, as it was tied to timeout */
            break;
        case picoquic_congestion_notification_rtt_measurement:
            c4_update_rtt(c4_state, ack_state->rtt_measurement, current_time);
            if (c4_state->alg_state == c4_initial) {
                c4_initial_handle_rtt(path_x, c4_state, notification, ack_state->rtt_measurement, current_time);
                c4_apply_rate_and_cwin(path_x, c4_state);
            }
            else {
                c4_handle_rtt(cnx, path_x, c4_state, ack_state->rtt_measurement, current_time);
            }
            break;
        case picoquic_congestion_notification_lost_feedback:
            break;
        case picoquic_congestion_notification_cwin_blocked:
            break;
        case picoquic_congestion_notification_reset:
            c4_reset(c4_state, path_x, c4_state->option_string, current_time);
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
    *cc_param = c4_state->nominal_max_rtt;
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
