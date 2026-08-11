# Addressing competition and latency by tweaking C4 Max RTT

Our analysis of the draft-4 version of C pointed a fairness issue.
That version of C4 would often grab more than 70% of the available
capacity, and sometimes more than 80%, which has a serious
impact on the competing connection. That version also results in
higher RTT and thus higher queues than Cubic or BBR in the same
scenarios. Attempts to lower the aggressiveness of C4 by
tuning the probing and pushing parameters were not successful.
We thus switch our attention to the handling of the "nominal max RTT"
parameter.

First, let's look at how thw nominal max RTT is updated. The relevant parts of the code are:

* In the "exit_initial" function, the nominal max RTT is set to
  "ssthresh * 1000000 / c4_state->nominal_rate"

* In the function `c4_update_min_max_rtt`, the nominal max RTT is updated
  if the last RTT measurement was not the result of pushing or probing:

~~~
        uint64_t corrected_max = (c4_state->era_max_rtt < c4_state->running_min_rtt + C4_MAX_JITTER) ?
            c4_state->era_max_rtt : c4_state->running_min_rtt + C4_MAX_JITTER;

        if (corrected_max > c4_state->nominal_max_rtt) {
            c4_state->nominal_max_rtt = corrected_max;
        }
        else {
            /* If not growing, slowly diminish the max rtt */
            c4_state->nominal_max_rtt = (7 * c4_state->nominal_max_rtt + corrected_max) / 8;
        }
~~~

* In the function `c4_notify_congestion`, the nominal max RTT is reduced in case
  of congestion:

~~~
    if (c4_state->alg_state == c4_recovery) {
        if (c4_state->alpha_1024_current == C4_ALPHA_RECOVER_1024) {
            /* Congestion notification after entering recovery 
             * indicates that queues are building up. It is thus
             * prudent to decrease "alpha_current" and to spend a bit
             * more time in recovery, to reduce these queues. */
            c4_state->alpha_1024_current = C4_ALPHA_RECOVER2_1024;
            c4_state->era_sequence = picoquic_cc_get_sequence_number(path_x->cnx, path_x);
            C4_LOGGER(path_x, 0, c4_state, NULL, beta, c_mode);
        }
        if (c_mode == c4_congestion_ecn) {
            c4_state->excess_ce_after_push = 1;
        }
    }
    else
        if (c4_state->alg_state != c4_probing && c4_state->alg_state != c4_pushing) {
            c4_state->nominal_rate -= MULT1024(beta, c4_state->nominal_rate);
            if (c_mode == c4_congestion_loss) {
                c4_state->nominal_max_rtt -= MULT1024(beta, c4_state->nominal_max_rtt);
                if (c4_state->nominal_max_rtt < C4_MAX_RTT_MIN) {
                    c4_state->nominal_max_rtt = C4_MAX_RTT_MIN;
                }
                c4_state->delay_threshold = c4_delay_threshold(c4_state);
            }
            C4_LOGGER(path_x, 0, c4_state, NULL, beta, c_mode);
        }
        c4_enter_recovery(path_x, c4_state, c_mode);
~~~

This code is based on the following principles:

1. Initialize nominal max RTT at the end of the initial phase based on ssthresh,
2. If a measurement in a non-growth phase exceeds the nominal max RTT,
   set the nominal max RTT to the new value.
3. If the largest measurement durin a non growth era is lower than the max
   RTT, set the max RTT to the 7/8th point between the measurement and the
   previous value.
3. If a congestion is detected in a non-growth phase, decrease the nominal max RTT
   by the coefficient beta.

In non-pushing eras, an excessive RTT measurement results in an update of
the max RTT. In pushing eras, and excessive measurement may trigger a congestion
signal that will sop the pushing and cause a reduction of the max RTT.
If the max RTT is over-estimated, the pushing will happen later, or not at all.
Could that impact competition and measured RTT? We can try to find out by
letting the max RTT grow slower or decrease faster. One way to grow slower would
be to increase the max RTT by a 1/2, 3/4 or 7/8 combination of the previous
value and last measurement.

The following table shows the results of a test where we set the max RTT to the
average of the previous value and the last measurement.

|  top 90% load for compete tests| c4 | bbr | cubic | 1/2 RTT growth |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% | 78% |
| vs_c4 |  52% | 31% | 33% | 52% |
| vs_cubic |  66% | 31% | 46% | 66% |
| after_c4 |  40% | 34% | 32% | 37% |
| before_c4 |  72% | 50% | 67% | 68% |
| vs_c4_lg |  61% | 58% | 58% | 60% |
| vs_c4_lg2 |  64% | 61% | 60% | 62% |
| vs_bbr_lg |  81% | 59% | 81% | 80% |
| vs_bbr_lg2 |  78% | 69% | 61% | 77% |
| vs_cubic_lg |  78% | 58% | 60% | 69% |
| vs_cubic_lg2 |  76% | 81% | 63% | 71% |

These measurements are encouraging. The long term competition with Cubic now leaves about
30% of the bandwidth to Cubic, instead of 23% before. But while the
competition with BBR becomes a little bit more fair, C4 only leaves about 20% of the bandwidth to BBR.
Let's see if we can improve that by not only increasing slower, but also decreasing faster.
The following table shows the results of a test where if the measurement is lower than the
value we also set the max RTT to the 3/4th point between the two.

|  top 90% load for compete tests| c4 | bbr | cubic | 1/2 RTT growth, 3/4 RTT decrease |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% | 78% |
| vs_c4 |  52% | 31% | 33% | 52% |
| vs_cubic |  66% | 31% | 46% | 66% |
| after_c4 |  40% | 34% | 32% | 38% |
| before_c4 |  72% | 50% | 67% | 66% |
| vs_c4_lg |  61% | 58% | 58% | 60% |
| vs_c4_lg2 |  64% | 61% | 60% | 64% |
| vs_bbr_lg |  81% | 59% | 81% | 77% |
| vs_bbr_lg2 |  78% | 69% | 61% | 77% |
| vs_cubic_lg |  78% | 58% | 60% | 64% |
| vs_cubic_lg2 |  76% | 81% | 63% | 71% |

The improvements are modest. We notice that the competion with BBR is a bit more fair, albeit
not by much. The competition with Cubic is also a bit more fair, but not by much either.
More importantly, the performance of C4 in the Wi-Fi tests is notably worse. The factor 7/8
is a compromise between decreasing the RTT too fast, which degrade performance in high
jitter situations like Wi-Fi, and decreasing it too slowly, which may cause queues
in competition scenarios. It is probably best to not change it, and look at other
potential solutions.

