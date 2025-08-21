---
title: "Design of Christian's Congestion Control Code (C4)"
abbrev: "C4 Design"
category: info

docname: draft-huitema-ccwg-c4-design-latest
submissiontype: IETF
number:
date:
consensus: true
ipr: trust200902
area: "Web and Internet Transport"
keyword:
 - C4
 - Congestion Control
 - Realtime Communication
 - Media over QUIC

author:
 -
   ins: C. Huitema
   name: Christian Huitema
   org: Private Octopus Inc.
   email: huitema@huitema.net
 -
   ins: S. Nandakumar
   name: Suhas Nandakumar
   organization: Cisco
   email: snandaku@cisco.com
 -
   ins: C. Jennings
   name: Cullen Jennings
   organization: Cisco
   email: fluffy@iii.ca

normative:
informative:
   RFC9000:
   I-D.ietf-moq-transport:
   RFC9438:
   I-D.ietf-ccwg-bbr:
   RFC6817:
   RFC6582:

   TCP-Vegas:
    target: https://ieeexplore.ieee.org/document/464716
    title: "TCP Vegas: end to end congestion avoidance on a global Internet"
    date: "31 October 1995"
    seriesinfo: "IEEE Journal on Selected Areas in Communications ( Volume: 13, Issue: 8, October 1995)"
    author:
     -
       ins: L.S. Brakmo
     -
       ins: L.L. Peterson

   HyStart:
     target: https://doi.org/10.1016/j.comnet.2011.01.014
     title: "Taming the elephants: New TCP slow start"
     date: "June 2011"
     seriesinfo: Computer Networks vol. 55, no. 9, pp. 2092-2110
     author:
      - 
        ins: S. Ha
      -
        ins: I. Rhee

   Cubic-QUIC-Blog:
    target: https://www.privateoctopus.com/2019/11/11/implementing-cubic-congestion-control-in-quic/
    title: "Implementing Cubic congestion control in Quic"
    date: "Nov 18, 2019"
    seriesinfo: "Christian Huitema's blog"
    author:
    -
      ins: C. Huitema

   I-D.ietf-quic-ack-frequency:

   Wi-Fi-Suspension-Blog:
    target: https://www.privateoctopus.com/2023/05/18/the-weird-case-of-wifi-latency-spikes.html
    title: "The weird case of the wifi latency spikes"
    date: "May 18, 2023"
    seriesinfo: "Christian Huitema's blog"
    author:
    -
      ins: C. Huitema

   I-D.irtf-iccrg-ledbat-plus-plus:

--- abstract

Christian's Congestion Control Code is a new congestion control
algorithm designed to support Real-Time applications such as
Media over QUIC. It is designed to drive towards low delays,
with good support for the "application limited" behavior
frequently found when using variable rate encoding, and
with fast reaction to congestion to avoid the "priority
inversion" happening when congestion control overestimates
the available capacity. The design emphasizes simplicity and
avoids making too many assumption about the "model" of
the network.

--- middle

# Introduction

Christian's Congestion Control Code (C4) is a new congestion control
algorithm designed to support Real-Time multimedia applications, specifically
multimedia applications using QUIC {{RFC9000}} and the Media
over QUIC transport {{I-D.ietf-moq-transport}}. These applications
require low delays, and often exhibit a variable data rate as they
alternate high bandwidth requirements when sending reference frames
and lower bandwith requirements when sending differential frames.
We translate that into 3 main goals:

- Drive towards low delays,
- Support "application limited" behavior,
- React quickly to changing network conditions.

The design of C4 is inspired by our experience using different
congestion control algorithms for QUIC,
notably Cubic {{RFC9438}}, Hystart {{HyStart}}, and BBR {{I-D.ietf-ccwg-bbr}},
as well as the study
of delay-oriented algorithms such as TCP Vegas {{TCP-Vegas}}
and LEDBAT {{RFC6817}}. In addition, we wanted to keep the algorithm
simple and easy to implement.

From this goals, we derive a series of design choices.

# React to delays

If we want to drive for low delays, the obvious choice is to observe
react to delay variations. Our baseline is to use the reaction to
delays found in congestion control algorithms like TCP Vegas or LEDBAT:

- monitor the current RTT and the min RTT
- if the current RTT sample exceed the min RTT by more than a preset
  margin, treat that as a congestion signal.

The "preset margin" is set by default to 10 ms in TCP Vegas and LEDBAT.
That was adequate when these algorithms were designed, but it can be
considered excessive in high speed low latency networks.
For C4, we set it to the lowest of 1/8th of the min RTT and 25ms.

The min RTT itself is measured over time. We know from deployment
of Hystart that delay based algorithms are very sensitive to an
underestimation of min RTT, which can be cause for example by delay jitter
and ACK compression. Instead of simply retraining the minimum of
all measurements, we apply filtering.

After applying filtering, the detection of congestion by comparing
delays to min RTT plus margin works well, except in two conditions:

- if the C4 connection is competing with a another connection that
  does not react to delay variations, such as a connection using Cubic,
- if the network exhibits a lot of latency jitter, as happens on
  some Wi-Fi networks.

We also know that if several connection using delay-based algorithms
compete, the competition is only fair if they all have the same
estimate of the min RTT. We handle that by using a "periodic slow down"
mechanism.

## Filtering of Delay measurements

When testing an implementation of Cubic in QUIC in 2019, we noticed
that delay jitter was causing Hystart to exit early, resulting in
poor performance for the affected connection (see {{Cubic-QUIC-Blog}}).
There was a double effect:

- Latency jitter sometimes resulted in measurements exceeding the
  target delay, causing Hystart to exit,
- Latency jitter also resulted sometimes in measurements lower
  than the min RTT, causing the delay threshold to be set at
  an excessively low value.

We corrected the issue by implementing a low pass filter of the
the delay measurements. The low pass filter retained the last N
measurements. We modified delay tests as follow:

- We only considered a measurement as exceeding the delay target
  if all last N measurements exceeded that target,
- We only updated the min RTT if the maximum of the last
  N measurements exceeded the min RTT.

There is of course a tension between the number of samples
considered (the value N) and the responsiveness of delay-based
congestion control. The delay-based congestion notification will
only happen after N measurements. This depends on the frequency
of acknowledgements, which itself depends on configuration
parameters of the QUIC implementation. This frequency is itself
compromise: too frequent ACKs cause too many interruptions
and slow down the transmission, but spacing ACKs too much
means that detection of packet losses will be delayed.
QUIC ACK Frequency draft {{I-D.ietf-quic-ack-frequency}} allows
sender to specify a maximum delay and a maximum number of packets
between ACKs, and we can expect different implementations
to use different parameters.
In our early implementation, we set N=7, because we
assume that we will get at least 8 ACKs per RTT most of the
time. We might want to reduce that number when the congestion
window is small, for example smaller than 16 packets, but
we have not tested that yet.

## Managing Competition with Loss Based Algorithms

Competition between Cubic and a delay based algorithm leads to Cubic
consuming all the bandwidth, and the delay based connection starving.
This phenomenon force TCP Vegas to only be deployed in controlled
environments, in which it does not have to compete with
TCP Reno {{RFC6582}} or Cubic. 

We handles this competition issue by using a simple detection algorithm.
If C4 detect competition with a loss based algorithm, it switches
to "pig war" mode and stops reacting to changes in delays -- it will
instead only react to packet losses and ECN signals. In that mode,
we use another algorithm to detect when the competition has ceased,
and switch back to the delay responsive mode.

In our initial deployments, we detect competition when delay based
congestion notifications leads to CWND reduction for 3 consecutive
RTT. The assumption is that if the competition reacted to delays
variations, it would have reacted to the delay increases before
3 RTT.

Our initial exit competition algorithm is simple. C4 will exit the
"pig war" mode if the available bandwidth increases..

## Handling Chaotic Delays

Some Wi-Fi network exhibit spikes in latency. These spikes are
probably what caused the delay jitter discussed in
{{Cubic-QUIC-Blog}}. We discussed them in more details in
{{Wi-Fi-Suspension-Blog}}. We are not sure about the
mechanism behind these spikes, but we have noticed that they
mostly happen when several adjacent Wi-Fi networks are configured
to use the same frequencies and channels. In these configurations,
we expect the hidden node problem to result in some collisions.
The Wi-Fi layer 2 retransmission algorithm takes care of these
losses, but apparently uses an exponential back off algorithm
to space retransmission delays in case of repeated collisions.
When repeated collisions occur, the exponential backoff mechanism
can cause large delays. The Wi-Fi layer 2 algorithm will also
try to maintain delivery order, and subsequent packets will
be queued behind the packet that caused the collisions.

We detect the advent of such "chaotic delay jitter" by computing a running
estimate of the max RTT. We measure the max RTT observed in each round trip,
to obtain the "era max RTT". We then compute an exponentially averaged
"nominal max RTT":

~~~
nominal_max_rtt = (7 * nominal_max_rtt + era_max_rtt) / 8;
~~~

If the nominal max RTT is more than twice the min RTT, we set the
"chaotic jitter" condition. When that condition is set, instead of
detecting delay based congestion by comparing the delay measurements
to min RTT, we compare them to a "target RTT", computed as:

~~~
target_rtt = (3 * min_RTT + 1*nominal_max_rtt)/4
~~~

The network conditions can evolve over time. C4 will keep monitoring
the nominal max RTT, and will reset the "chaotic jitter" condition
if nominal max RTT decreases below a threshold of 1.5 times the
min RTT.

## Monitor min RTT

Delay based algorithm like CWND rely on a correct estimate of the
min RTT. They will naturally discover a reduction in the min
RTT, but detecting an increase in the max RTT is difficult.
There are known failure modes when multiple delay based
algortihms compete, in particular the "late comer advantage".

The connections ensure that their min RTT is valid by
occasionally entering a "slowdown" period, during which they set
CWND to half the nominal value. This is similar to
the "Probe RTT" mechanism implemented in BBR, or the
"initial and periodic slowdown" proposed as extension
to LEDBAT in {{I-D.irtf-iccrg-ledbat-plus-plus}}. In our
implementation, the slowdown occurs if more than 5
seconds have elapsed since the previous slowdown, or
since the last time the min RTT was set.

The measurement of min RTT in the period
that follows the slowdown is considered a "clean"
measurement. If two consecutive slowdown periods are
followed by clean measurements larger than the current
min RTT, we detect an RTT change and reset the
connection. If the measurement results in the same
value as the previous min RTT, C4 continue normal
operation.

Some applications exhibit periods of natural slow down. This
is the case for example of multimedia applications, when
they only send differentially encoded frames. Natural
slowdown is detected if an application sends less than
half the nominal CWND during a period, and more than 4 seconds
have elapsed since the previous slowdown or the previous
min RTT update. The measurement that follows a natural
slowdown is also considered a clean measurement.

A slowdown period corresponds to a reduction in offered
traffic. If multiple connections are competing for the same
bottleneck, each of these connections may experience cleaner
RTT measurements, leading to equalization of the min RTT
observed by these connections.

# Keep it simple

We develop C4 partly as a reaction to the complexity of BBR, which tends to
increase with each successive release of the BBR draft. BBR controls both
the pacing rate, which tracks the expected bottleneck bandwidth, and the
congestion window (CWND), which limits the number of bytes in flight according
to pacing rate, min RTT and estimate of max bytes in flight supported by
the network. This leads to complex interaction between "short term" and
"long term" parameters. Instead, C4 uses a single control parameter,
the congestion window. We compute the pacing rate based on the congestion
window and the target RTT, which is mostly based on the min RTT.

# Make before break

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

# React to Congestion Signals

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
    nominal_CWND = (1-beta)*nominal_CWND
~~~
The cofficient `beta` differs depending on the nature of the congestion
signal. For packet losses, it is set to `1/4`, similar to the
value used in Cubic. For delay based losses, it is proportional to the
difference between the measured RTT and the target RTT divided by
the acceptable margin, capped to `1/4`. If the signal
is an ECN/CE rate, we may
use a proportional reduction coefficient in line with
the L4S specification, again capped to `1/4`.

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
  the `nominal_CWIN`, the next `pushing` will happen at 5.25%
- If pushing at 6.25% did result in some increase of the `nominal_CWIN`,
  the next `pushing` will happen at 25%, otherwise it will
  remain at 6.25%
- If three consecutive `pushing` periods result in significant
  increases, we detect that the underlying network conditions
  have changed, and we reenter the `startup` phase.

By exception, we will always push at 25% in the `pig war` mode.

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
  one round trip. 

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
---------|----------|----
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

We could also express this fractions based on the measured bandwidth, `nominal_CWND`
divided by `target_RTT`, which would make fairness less dependent on path RTT.


# Security Considerations

We do not believe that C4 introduce new security issues. Or maybe there are,
such as what happen if applications can be fooled in going to fast and
overwhelming the network, or going to slow and underwhelming the application.
Discuss!

# IANA Considerations

This document has no IANA actions.

--- back

# Acknowledgments
{:numbered="false"}

TODO acknowledge.







