# Design of Christian's Congestion Control Code (C4)

C4 is designed to support multimedia applications. These applications
require low delays, and often exhibit a varaible data rate as they
alternate high bandwidth requirements when sending reference frames
and lower bandwith requirements when sending differential frames.
We translate that into 3 main goals:

- Drive towards low delays,
- Support "application limited" behavior,
- React quickly to changing network conditions.

In addition, we want to do all that while keeping the protocol
simple and easy to implement.

From this goals, we derive a series of design choices.

## Keep it simple

We develop C4 partly as a reaction to the complexity of BBR, which tends to
increase with each successive release of the BBR draft. BBR controls both
the pacing rate, which tracks the expected bottleneck bandwidth, and the
congestion window (CWND), which limits the number of bytes in flight according
to pacing rate, min RTT and estimate of max bytes in flight supported by
the network. This leads to complex interaction between "short term" and
"long term" parameters. Instead, C4 uses a single control parameter,
the congestion window. We compute the pacing rate based on the congestion
window and the target RTT, which is mostly based on the min RTT.

## React to delays

If we want to drive for low delays, the obvious choice is to observe
react to delay variations. Our baseline is to use the reaction to
delays found in congestion control algorithms like TCP VEGAS or LEDBAT:

- monitor the current RTT and the min RTT
- if the current RTT sample exceed the min RTT by more than a preset
  margin, treat that as a congestion signal.

We know that this simple algorithm fails in two conditions:

- if the C4 connection is competing with a another connection that
  does not react to delay variations, such as a connection using Cubic,
- if the network exhibits a lot of latency jitter, as happens on
  some Wi-Fi networks.

Competition between Cubic and a delay based algorithm leads to Cubic
consuming all the bandwidth, and the delay based connection starving.
We handles that by detecting the competition scenario when delay based
congestion notifications leads to CWND reduction for 3 consecutive
RTT. In that case, C4 switches to "pig war" mode and stops reacting
to changes in delays. C4 will exit the `pig_war` mode if the bandwidth
raises above the initial value.

Some Wi-Fi network exhibit spikes in latency, which we believe are
caused by packet losses due to collisions when adjacent Wi-Fi
networks operate on the same channel. Collisions and packet losses
are corrected by a link layer protocol that uses the exponential backoff
to increase delays between successive repeats of a packet. That causes
large delay variations. We detect that by computing a running
estimate of max RTT. If the running estimate of max RTT is more than 
double `min_RTT`, instead of setting
`target_RTT = min_RTT`, we use:
~~~
target_rtt = (3 * min_RTT + 1*max_RTT_estimate)/4
~~~
We then detect delay based congestion by comparing RTT samples to
`target_RTT + margin`.

## Make before break

We update the CWND value during `push` periods, during which we
normally increase the value of CWND by 25% -- or by 100% during
the startup phase. We then monitor the number of bytes acknowledged
at the end of the period, i.e., between the acknowledgement
of the last packet in the previous period and the acknowledgement
of the last packet of this period. This number
of acknowledged bytes reflect the capacity of the network, but
the increased transmission rate during push may also force
some queuing. We correct the initial estimate for the
queuing effect using the formula:
~~~
if not in pig-war mode:
    corrected_bytes_acked = bytes_acked_in_period * target_rtt /
                     max(target_rtt, period_duration)
else:
    corrected_bytes_acked = bytes_acked_in_period
~~~
We then compare `corrected_bytes_acked` to the nominal CWND, and update
the CWND only if `corrected_bytes_acked` is greater:
~~~
if corrected_bytes_acked > CWND:
    nominal_CWND = corrected_bytes_acked
~~~
In normal operation, we ensure that the CWND stays within network
capacity by setting:
~~~
    CWND = nominal_CWND
~~~
We only use a larger CWND during the `push` phases, setting:
~~~
    CWND = (1+alpha)* nominal_CWND
~~~

We also use the make before break strategy during the initial
`startup` period, during which we set:
~~~
    alpha_startup = 100%
~~~
We will exit the startup period if the value of
`nominal_CWND` does not increase by at least 10% for 3
consecutive RTT.

## React to Congestion Signals

The `nominal_CWND` formula ensure that CWND will grow if capacity
is found available during a `push` period. However, changing
network conditions may happen at any time. C4 detects these
changing conditions by monitoring 3 congestion control
signals:

1. Excessive increase of measured RTT (except in `pig war` mode),
2. Excessive rate of packet losses,
3. Excessive rate of ECN/CE marks

If any of these signals is detected, we will enter a `recovery`
period. On entering recovery, we will reduce the `nominal_CWND`
if the congestion happened outside of a `push` period:
~~~
    // on congestion detected:
    nominal_CWND = beta*nominal_CWND
~~~
The cofficient `beta` is normally set to `3/4`, similar to the
value used in Cubic. If the signal is an ECN/CE rate, we may
use a proportional reduction coefficient in line with
the L4S specification.

During the recovery period, CWND is set to the new value of
`nominal_CWND`. The recovery period ends when the first packet
sent after entering recovery is acknowledged. Congestion
signals are processed when entering recovery; further signals
are ignored until the end of recovery.

QUIC normally detect losses by observing gaps in the sequences of acknowledged
packet. That's a robust signal. QUIC will also use inject "Probe time out"
packets if the PTO timeout elapses before the last sent packet has not been acknowledged.
This is not a robust congestion signal, because the delay jitter may also cause
PTO timeouts. When testing in "high jitter" conditions, we realized that we should
not change the state of C4 for losses detected solely based on timer, and
only react to those losses that are detected by gaps in acknowledgements.

## Variable Pushing Rate

C4 tests for avaiable bandwidth at regular `pushing` intervals.
Testing is compromise between operating at the available
capacity and risking building queues. Testing at 25% mimics what BBR
is doing, but may be less than ideal for real time applications.
We manage that compromise by adopting a variable `pushing` rate:

- If pushing at 25% did not result in a significant increase of
  the `nominal_CWIN`, the next `pushing` will happen at 10%
- If pushing at 10% did result in some increase of the `nominal_CWIN`,
  the next `pushing` will happen at 25%, otherwise it will
  remain at 10%
- If three consecutive `pushing` periods result in significant
  increases, we detect that the underlying network conditions
  have changed, and we reenter the `startup` phase.

By exception, we will always push at 25% in the `pig war` mode.

## Monitor min RTT

Delay based algorithm like CWND rely on a correct estimate of the
min RTT. They will naturally discover a reduction in the min
RTT, but detecting an increase in the max RTT is difficult.
There are know failure mode when multiple delay based
connection compete, in particular the "late comer advantage".
The connections ensure that their min RTT is valid by
entering a `slowdown` period, during which they set
CWND to half the nominal value.

Some applications exhibit periods of natural slow down. This
is the case for example of multimedia applications, when
they only send differentially encoded frames. Natural
slow down is detected if an application sends less than
half the nominal CWND during a period.

The connection will enter a slowdown period if at least
5 seconds have passed since the last forced or natural
slowdown. The measurement of min RTT in the period
that follows the slowdown is considered a "clean"
measurement. If two consecutive slowdown periods are
followed by clean measurements larger than the current
min RTT, we detect and RTT change and reset the
connection. If not, we just continue normal processing.

A slowdown period corresponds to a reduction in offered
traffic. If multiple connections are competing for the same
bottleneck, each of those connections may experience cleaner
RTT measurements, leading to equalization of the `min_RTT`
observed by these connections.

## Supporting Application Limited Connections

In the previous sections, we mentioned a series of details
meant for supporting application limited connections:

- The CWND can only become lower if a congestion signal is received.
  That means a limited connection that is not experiencing congestion
  will continue operating with the nominal congestion window.

- Transition to `pushing` only happens when the application is not
  congestion limited. This means the application will have a chance
  to test capacity limits when it has data to send.

- There will ne no transition to slowdown if the application is
  some of the time operating at less than the half the maximum
  rate.



## Simple state machine

The state machine for C4 has the following states:

* `startup`: the initial state, during which the CWND is
  set to twice the `nominal_CWND`. The connection
  exits startup if the `nominal_cwnd` does not
  increase for 3 consecutive round trips. When the
  connection exits startup, it enters `recovery`.
* `recovery`: the connection enters that state after
  `startup`, `pushing`, or a congestion detection in
  a `cruising` state. It remains in that state for
  at least one roundtrip, until the first packet sent
  in `discovery` is acknowledged. Once that happens,
  the connection enters `slowdown` if no `slowdown`
  has been experienced for 5 seconds, or `cruising`
  otherwise.
* `cruising`: the connection is sending using the
  `nominal_cwnd` value. If congestion is detected,
  the connection exits cruising and enters
  `recovery` after lowering the value of
  `nominal_cwnd`. If after a roundtrip the application
  sent fewer than 1/2 of `nominal_cwnd`, and if the
  last `slowdown` occured more than 4 seconds ago,
  the connection moves to the `post-slowdown` state.
  Otherwise, the connection will
  remain in `cruising` state until a sufficient
  number of bytes have been acknowledged, and
  the connection is not `app limited`. At that
  point, it enters `pushing`.
* `pushing`: the connection is using a CWND 25%
  larger than `nominal_CWND`. It remains in that state
  for one round trip, i.e., until the first packet
  send while `pushing` is acknowledged. At that point,
  it enters the `recovery` state. 
* `slowdown`: the connection is using CWND set to
  1/2 of `nominal_CWND`. It exits `slowdown` after
  one round trip, and moves to the `post-slowdown` state.
* `post-slowdown`: the connection is using CWND set to
  `nominal_CWND`. It remains in that state until the
  last byte sent during the `slowdown` state is
  acknowledged. If the minimum RTT observed during
  two `post-slowdown` states is larger than `min_RTT`,
  the `min_RTT` is set to that value, and the connection
  moves back to `startup`. Otherwise, the connection moves
  back to cruising state.

## Driving for fairness

The duration of `cruising` is expressed as a number of bytes.
This number of bytes is computed with two goals:

* The potential increase of 25% divided by the total
  number of bytes sent during recovery, cruising and
  pushing should match the aggressiveness of RENO when
  operating at low bandiwdth (lower than 34 Mbps).
* Connection using large values of CWND should be less
  aggressive than those with smaller value, so that
  all connections sharing the same bottleneck should
  eventually converge to similar CWND values.

We achieve those two goals using the following formula:
~~~
    target = nominal_CWND*7* (1 + 
             (max(11, min(28, log2(nominal_CWND))) - 11)/(28 - 11))
~~~
The constants `7`, `11` and `28` in the formula are explained as
follow:

- The number 7 corresponds to 7 RTT. Depending of bandwidth, the
  connection will remain between 7 and 14 RTT in cruise state.
- The value 11 corresponds to 2048 bytes. We use that as a minimum.
- The value 28 corresponds to 268,435,456 bytes, e.g., a connection
  at 10 Gbps operating with an RTT a little above 200ms.

For example, assuming an RTT of 50 ms, we will see cruising
duration in RTT varying with the bandwidth in bps as:

BW(bps) | BDP(bytes) | Nb RTT
---------|----------|----+
1.00E+05 | 6.25E+02 | 7.0
2.00E+05 | 1.25E+03 | 7.0
5.00E+05 | 3.13E+03 | 7.3
1.00E+06 | 6.25E+03 | 7.7
2.00E+06 | 1.25E+04 | 8.1
5.00E+06 | 3.13E+04 | 8.6
1.00E+07 | 6.25E+04 | 9.0
2.00E+07 | 1.25E+05 | 9.4
5.00E+07 | 3.13E+05 | 10.0
1.00E+08 | 6.25E+05 | 10.4
2.00E+08 | 1.25E+06 | 10.8
5.00E+08 | 3.13E+06 | 11.4
1.00E+09 | 6.25E+06 | 11.8
2.00E+09 | 1.25E+07 | 12.2
5.00E+09 | 3.13E+07 | 12.7
1.00E+10 | 6.25E+07 | 13.1
2.00E+10 | 1.25E+08 | 13.5
5.00E+10 | 3.13E+08 | 14.0
1.00E+11 | 6.25E+08 | 14.0

We could have expressed this in fractions of RTT instead of number of bytes,
which might be more fair for application limited connection.











