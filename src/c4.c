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


#define c4_MIN_ACK_DELAY_FOR_BANDWIDTH 5000
#define c4_BANDWIDTH_FRACTION 0.5
#define c4_REPEAT_THRESHOLD 4
#define c4_BETA 0.125
#define c4_BETA_HEAVY_LOSS 0.5
#define c4_pushing_ALPHA 0.25
#define c4_DELAY_THRESHOLD_MAX 25000
#define c4_NB_PERIOD 6
#define c4_PERIOD 1000000
#define PICOQUIC_CC_ALGO_NUMBER_C4 8

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
    uint64_t end_of_freeze; /* When to exit the freeze state */
    uint64_t last_ack_time;
    uint64_t ack_interval;
    uint64_t nb_bytes_ack;
    uint64_t nb_bytes_ack_since_rtt; /* accumulate byte count until RTT measured */
    uint64_t end_of_epoch;
    uint64_t recovery_sequence;
    uint64_t rtt_min;
    uint64_t delay_threshold;
    uint64_t rolling_rtt_min; /* Min RTT measured for this epoch */
    uint64_t last_rtt_min[c4_NB_PERIOD];
    int nb_cc_events;
    unsigned int last_freeze_was_timeout : 1;
    unsigned int last_freeze_was_not_delay : 1;
    unsigned int rtt_min_is_trusted : 1;
    picoquic_min_max_rtt_t rtt_filter;
} c4_state_t;

uint64_t c4_delay_threshold(uint64_t rtt_min)
{
    uint64_t delay = rtt_min / 8;
    if (delay > c4_DELAY_THRESHOLD_MAX) {
        delay = c4_DELAY_THRESHOLD_MAX;
    }
    return delay;
}

void c4_reset(c4_state_t* c4_state, picoquic_path_t* path_x, uint64_t current_time)
{
    memset(c4_state, 0, sizeof(c4_state_t));
    c4_state->alg_state = c4_initial;
    c4_state->rtt_min = path_x->smoothed_rtt;
    c4_state->rolling_rtt_min = c4_state->rtt_min;
    c4_state->delay_threshold = c4_delay_threshold(c4_state->rtt_min);
    c4_state->end_of_epoch = current_time + c4_PERIOD;
    path_x->cwin = PICOQUIC_CWIN_INITIAL;
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
    
    if (c4_state != NULL) {
        memset(c4_state, 0, sizeof(c4_state_t));
        c4_state->alg_state = c4_initial;
        c4_state->rtt_min = path_x->smoothed_rtt;
        c4_state->rolling_rtt_min = c4_state->rtt_min;
        c4_state->delay_threshold = c4_delay_threshold(c4_state->rtt_min);
        c4_state->end_of_epoch = current_time + c4_PERIOD;
        path_x->cwin = PICOQUIC_CWIN_INITIAL;
    }

    path_x->congestion_alg_state = (void*)c4_state;
}

/* Reaction to ECN/CE or sustained losses.
 * This is more or less the same code as added to bbr.
 *
 * This code is called if an ECN/EC event is received, or a timeout
 * event, or a lost event indicating a high loss rate
 */
static void c4_notify_congestion(
    picoquic_cnx_t* cnx,
    picoquic_path_t* path_x,
    c4_state_t* c4_state,
    uint64_t current_time,
    int is_delay,
    int is_timeout)
{
    if (c4_state->alg_state == c4_recovery &&
        (!is_timeout || !c4_state->last_freeze_was_timeout) &&
        (!is_delay || !c4_state->last_freeze_was_not_delay)) {
        /* Do not treat additional events during same freeze interval */
        return;
    }
    c4_state->last_freeze_was_not_delay = !is_delay;
    c4_state->last_freeze_was_timeout = is_timeout;
    c4_state->alg_state = c4_recovery;
    c4_state->end_of_freeze = current_time + c4_state->rtt_min;
    c4_state->recovery_sequence = picoquic_cc_get_sequence_number(cnx, path_x);
    c4_state->nb_cc_events = 0;

    if (is_delay) {
        path_x->cwin -= (uint64_t)(c4_BETA * (double)path_x->cwin);
    }
    else {
        path_x->cwin = path_x->cwin / 2;
    }

    if (is_timeout || path_x->cwin < PICOQUIC_CWIN_MINIMUM) {
        path_x->cwin = PICOQUIC_CWIN_MINIMUM;
    }

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
        if (c4_state->alg_state == c4_recovery && 
            (current_time > c4_state->end_of_freeze ||
                c4_state->recovery_sequence <= picoquic_cc_get_ack_number(cnx, path_x))) {
            if (c4_state->last_freeze_was_timeout) {
                c4_state->alg_state = c4_initial;
            }
            else {
                c4_state->alg_state = c4_pushing;
            }
            c4_state->last_freeze_was_not_delay = 0;
            c4_state->last_freeze_was_timeout = 0;

            c4_state->nb_cc_events = 0;
            c4_state->nb_bytes_ack_since_rtt = 0;
        }

        switch (notification) {
        case picoquic_congestion_notification_acknowledgement: 
            if (c4_state->alg_state != c4_recovery) {
                /* Count the bytes since last RTT measurement */
                c4_state->nb_bytes_ack_since_rtt += ack_state->nb_bytes_acknowledged;
                /* Compute pacing data. */
                picoquic_update_pacing_data(cnx, path_x, 0);
            }
            break;

        case picoquic_congestion_notification_ecn_ec:
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
                if (current_time > c4_state->end_of_epoch) {
                    /* If end of epoch, reset the min RTT to min of remembered periods,
                     * and roll the period. */
                    c4_state->rtt_min = UINT64_MAX;
                    for (int i = c4_NB_PERIOD - 1; i > 0; i--) {
                        c4_state->last_rtt_min[i] = c4_state->last_rtt_min[i - 1];
                        if (c4_state->last_rtt_min[i] > 0 &&
                            c4_state->last_rtt_min[i] < c4_state->rtt_min) {
                            c4_state->rtt_min = c4_state->last_rtt_min[i];
                        }
                    }
                    c4_state->delay_threshold = c4_delay_threshold(c4_state->rtt_min);
                    c4_state->last_rtt_min[0] = c4_state->rolling_rtt_min;
                    c4_state->rolling_rtt_min = c4_state->rtt_filter.sample_max;
                    c4_state->end_of_epoch = current_time + c4_PERIOD;
                }
                else if (c4_state->rtt_filter.sample_max < c4_state->rolling_rtt_min || c4_state->rolling_rtt_min == 0) {
                    /* If not end of epoch, update the rolling minimum */
                    c4_state->rolling_rtt_min = c4_state->rtt_filter.sample_max;
                    if (c4_state->rolling_rtt_min < c4_state->rtt_min) {
                        c4_state->rtt_min = c4_state->rolling_rtt_min;
                    }
                }
            }

            if (c4_state->alg_state != c4_recovery) {
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
                    double alpha = 1.0;
                    c4_state->nb_cc_events = 0;

                    if (c4_state->alg_state != c4_initial) {
                        alpha -= ((double)delta_rtt / (double)c4_state->delay_threshold);
                        alpha *= c4_pushing_ALPHA;
                    }

                    /* Increase the window if it is not frozen */
                    if (path_x->last_time_acked_data_frame_sent > path_x->last_sender_limited_time) {
                        path_x->cwin += (uint64_t)(alpha * (double)c4_state->nb_bytes_ack_since_rtt);
                    }
                    c4_state->nb_bytes_ack_since_rtt = 0;
                }
                else {
                    /* May well be congested */
                    c4_state->nb_cc_events++;
                    if (c4_state->nb_cc_events >= c4_REPEAT_THRESHOLD) {
                        /* Too many events, reduce the window */
                        c4_notify_congestion(cnx, path_x, c4_state, current_time, 1, 0);
                    }
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
