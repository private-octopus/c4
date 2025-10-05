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

   RFC9331:
   ICCRG-LEO:
    target: https://datatracker.ietf.org/meeting/122/materials/slides-122-iccrg-mind-the-misleading-effects-of-leo-mobility-on-end-to-end-congestion-control-00
    title: "Mind the Misleading Effects of LEO Mobility on End-to-End Congestion Control"
    date: "March 17, 2025"
    seriesinfo: "Slides presented at ICCRG meeting during IETF 122"
    author:
    -
      ins: Z. Lai
    -
      ins: Z. Li
    -
      ins: Q. Wu
    -
      ins: H. Li
    -
      ins: Q. Zhang

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

Christian's Congestion Control Code (C4) is a congestion control
algorithm designed to support Real-Time multimedia applications, specifically
multimedia applications using QUIC {{RFC9000}} and the Media
over QUIC transport {{I-D.ietf-moq-transport}}.

C4 assumes that the transport stack is
capable of signaling events such
as acknowledgements, RTT measurements, ECN signals or the detection
of packet losses. It also assumes that the congestion algorithm
controls the transport stack by setting the congestion window
(CWND) and the pacing rate.

C4 maintains a set of variables:

- nominal rate, an estimate of the data rate available to the flow,
- nominal MAX RTT, an estimate of the maximum RTT
  that can occur on the network in the absence of queues,
- running min RTT, an approximation of the min RTT for the flow,
- current state of the algorithm, which can be initial, recovery,
  cruising or pushing.

C4 divides the duration of the connection in a set of "eras",
each corresponding to a packet round trip. Transitions between protocol
states typically happen at the end of an era, except if the
transition is forced by a congestion event.


# C4 variables

## Per era variables

C4 keeps variables per era:
~~~
era_sequence; /* sequence number of first packet sent in this era */
alpha_1024_current; /* coefficient alpha used in the current state */
alpha_1024_previous; /* coefficient alpha used in the previous era */
era_max_rtt; /* max RTT observed during this era */
era_min_rtt; /* min RTT observed during this era */
~~~
These variables are initialized at the beginning of the era.


## Nominal rate

The nominal rate is an estimate of the bandwidth available to the flow.

On initialization, the nominal rate is set to zero, and default values
are used when setting the pacing rate and CWND for the flow.

C4 evaluates the nominal rate after acknowledgements are received
using the number of bytes acknowledged since the packet was sent
(`bytes_acknowledged`) and 
the time at which the acknowledged packet was sent (`time_sent`),
and the current time (`current_time`), as well as maximum transmission rate
computed from the current nominal rate and the coefficient
"alpha" used in the previous era (`alpha_previous`). If the
new measurement is larger than the current `nominal_rate`,
we increase that rate:
~~~
rate_estimate = min ( nominal_rate*alpha_previous,
    bytes_acknowledged /(current_time - time_sent)
nominal_rate = max(rate_estimate, nominal_rate)
~~~
Comparing to `nominal_rate*alpha_previous` protects against
overestimating the data rate because of transmission jitter.

## Nominal max RTT

The nominal max RTT is an estimate of the maximum RTT
that can occur on the path in the absence of queues.
The RTT samples observed for the flow are the sum of four
components:

* the latency of the path
* the jitter introduced by processes like link layer contention
  or link layer retransmission
* queuing delays caused by competing applications
* queuing delays introduced by C4 itself.

C4's goal is obtain a estimate of the combination and path latency
and maximum jitter. This is done by only taking measurements
when C4 is operating below the nominal transmission rate,
as happens for example in the recovery state. These measurements
will happen during the following era. C4 captures them
by recording the max RTT for packets sent in that era.
C4 will also progressively reduce the value of the
nominal max RTT over time, to account for changes in network
conditions.

~~~
/* on end of era */
if alpha_previous < 1.0:
    if era_max_rtt > nominal_max_rtt:
        nominal_max_rtt = era_max_rtt
    else:
        nominal_max_rtt =
          (7*nominal_max_rtt + era_max_rtt)/8
~~~

The ratio of decrease over time is tuned so that jitter
events will be remembered for a several of the
cruising-pushing-recovery cycles, which is enough time for the
next jitter event to happen, at least on Wi-Fi networks.



# States and Transition

# Handling of congestion signals


# Setting pacing rate, congestion window and quantum

C4 will compute a pacing
rate as the nominal rate multiplied by a coefficient that
depends on the state of the protocol, and set the CWND for
the path to the product of that pacing rate by the max RTT.

## General case

If the nominal rate or the nominal max RTT are not yet
assessed, C4 sets pacing rate, congestion window and
pacing quantum to initial values:

* pacing rate: set to the data rate of the outgoing interface,
* congestion window: set to the equivalent of 10 packets,
* congestion quantum: set to zero.

If the nominal rate or the nominal max RTT are both
assessed, C4 sets pacing rate, and congestion window 
to values that depends on these variables
and on a coefficient "alpha" defined for the current
state:

~~~
pacing_rate = alpha_current * nominal_rate
cwnd = max (pacing_rate * nominal_max_rtt, 2*MTU)
quantum = max ( min (cwnd / 4, 64KB), 2*MTU)
~~~

## Special case of startup phase

Discuss safe reuse


# Studying the reaction to delays {#react-to-delays}

TODO: we don't actually do that. But it is important to discuss
these techniques, and point out the issues. Discuss why
tracking the min RTT is problematic. Rewite the section
as a reasoning why this is an issue.

If we want to drive for low delays, the obvious choice is to
react to delay variations. Our first design was to use the reaction to
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
and ACK compression. Instead of simply retaining the minimum of
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
consuming all the bandwidth and the delay based connection starving.
This phenomenon force TCP Vegas to only be deployed in controlled
environments, in which it does not have to compete with
TCP Reno {{RFC6582}} or Cubic. 

We handle this competition issue by using a simple detection algorithm.
If C4 detect competition with a loss based algorithm, it switches
to "pig war" mode and stops reacting to changes in delays -- it will
instead only react to packet losses and ECN signals. In that mode,
we use another algorithm to detect when the competition has ceased,
and switch back to the delay responsive mode.

In our initial deployments, we detect competition when delay based
congestion notifications leads to CWND and rate
reduction for more than 3
consecutive RTT. The assumption is that if the competition reacted to delays
variations, it would have reacted to the delay increases before
3 RTT. However, that simple test caused many "false positive"
detections.

We have refined this test to start the pig war
if we have observed 4 consecutive delay-based rate reductions
and the nominal CWND is less than half the max nominal CWND
observed since the last "initial" phase, or if we have observed
at least 5 reductions and the nominal CWND is less than 4/5th of
the max nominal CWND.

We validated this test by comparing the
ratio `CWND/MAX_CWND` for "valid" decisions, when we are simulating
a competition scenario, and "spurious" decisions, when the
"more than 3 consecutive reductions" test fires but we are
not simulating any competition:

Ratio CWND/Max | valid | spurious
---------------|-------|----------
Average | 30% | 75%
Max | 49% | 100%
Top 25% | 37% | 91%
Media | 35% | 83%
Bottom 25% | 20% | 52%
Min | 12% | 25%
<50% | 100% | 20%

Our initial exit competition algorithm is simple. C4 will exit the
"pig war" mode if the available bandwidth increases.

## Handling Chaotic Delays

TODO: maybe start here. This is why we really want to have rate control,
but if we have rate control we can use larger windows.

Some Wi-Fi networks exhibit spikes in latency. These spikes are
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

We detect the advent of such "chaotic delay jitter" by computing
a running estimate of the max RTT. We measure the max RTT observed
in each round trip, to obtain the "era max RTT". We then compute
an exponentially averaged "nominal max RTT":

~~~
nominal_max_rtt = (7 * nominal_max_rtt + era_max_rtt) / 8;
~~~

If the nominal max RTT is more than twice the min RTT, we set the
"chaotic jitter" condition. When that condition is set, we stop
considering excess delay as an indication of congestion,
and we change
the way we compute the "current CWND" used for the controlled
path. Instead of simply setting it to "nominal CWND", we set it
to a larger value:

~~~
target_cwnd = alpha*nominal_cwnd +
              (max_bytes_acked - nominal_cwnd) / 2;
~~~
In this formula, `alpha` is the amplification coefficient corresponding
to the current state, such as for example 1 if "cruising" or 1.25
if "pushing" (see {{congestion}}), and `max_bytes_acked` is the largest
amount of bytes in flight that was succesfully acknowledged since
the last initial phase.

The increased `target_cwnd` enables C4 to keep sending data through
most jitter events. There is of course a risk that this increased
value will cause congestion. We limit that risk by only using half
the value of `max_bytes_ack`, and by the setting a
conservative pacing rate:

~~~
target_rate = alpha*nominal_rate;
~~~
Using the pacing rate that way prevents the larger window to
cause big spikes in traffic.

The network conditions can evolve over time. C4 will keep monitoring
the nominal max RTT, and will reset the "chaotic jitter" condition
if nominal max RTT decreases below a threshold of 1.5 times the
min RTT.

## Monitor min RTT

TODO: we will not in fact track the min RTT.

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


# React quickly to changing network conditions {#congestion}

TODO: only use rate, not CWND.

Our focus is on maintaining low delays, and thus reacting
to delay increases as explained in {{react-to-delays}}.
However, only relying on increased delays would be a mistake.
Experience with the early version of BBR showed that
completely ignoring packet losses can lead to very unfair
competition with Cubic. The L4S effort is promoting the use
of ECN feedback by network elements (see {{RFC9331}}),
which could well end up detecting congestion and queues
more precisely than the monitoring of end-to-end delays.
C4 will thus detect changing network conditions by monitoring
3 congestion control signals:

1. Excessive increase of measured RTT (except in pig war mode),
2. Excessive rate of packet losses (but not mere Probe Time Out, see {{no-pto}}),
3. Excessive rate of ECN/CE marks

If any of these signals is detected, C4 enters a "recovery"
state. On entering recovery, C4 reduces the `nominal_CWND`
and `nominal_rate` by a factor "beta":

~~~
    // on congestion detected:
    nominal_CWND = (1-beta)*nominal_CWND
    nominal_rate = (1-beta)*nominal_rate
~~~
The cofficient `beta` differs depending on the nature of the congestion
signal. For packet losses, it is set to `1/4`, similar to the
value used in Cubic. For delay based losses, it is proportional to the
difference between the measured RTT and the target RTT divided by
the acceptable margin, capped to `1/4`. If the signal
is an ECN/CE rate, we may
use a proportional reduction coefficient in line with
{{RFC9331}}, again capped to `1/4`.

During the recovery period, target CWND and pacing rate are set
to the new values of "nominal CWND" and "nominal rate".
The recovery period ends when the first packet
sent after entering recovery is acknowledged. Congestion
signals are processed when entering recovery; further signals
are ignored until the end of recovery.

Network conditions may change for the better or for the worse. Worsening 
is detected through congestion signals, but increases can only be detected
by trying to send more data and checking whether the network accepts it.
Different algorithms have done two ways: pursuing regular increases of
CWND until congestion finally occurs, like for example the "congestion
avoidance" phase of TCP RENO; or periodically probe the network
by sending at a higher rate, like the Probe Bandwidth mechanism of
BBR. C4 adopt the periodic probing approach, in particular
because it is a better fit for variable rate multimedia applications
(see details in {{limited}}).

## Do not react to Probe Time Out {#no-pto}

QUIC normally detect losses by observing gaps in the sequences of acknowledged
packet. That's a robust signal. QUIC will also inject "Probe time out"
packets if the PTO timeout elapses before the last sent packet has not been acknowledged.
This is not a robust congestion signal, because delay jitter may also cause
PTO timeouts. When testing in "high jitter" conditions, we realized that we should
not change the state of C4 for losses detected solely based on timer, and
only react to those losses that are detected by gaps in acknowledgements.

## Update the Nominal Rate and CWND after Pushing {#cwnd-update}

TODO: only the nominal rate.

C4 configures the transport with a larger rate and CWND
than the nominal CWND during "pushing" periods.
The peer will acknowledge the data sent during these periods in
the round trip that followed.

When we receive an ACK for a newly acknowledged packet number N,
we compute the number of bytes acknowledged
between the acknowledgement received before N was sent and
the acknowledgement of the last packet of this period. 
We use that number to compute the observed data rate as:

~~~
observed_rate = bytes_acked / max(observed_rtt, min_rtt)
~~~
The observed RTT is computed as the delay between the time
the acknowledged packet was sent and the time at which it was received.
Using `max(rtt_sample, min_rtt)` in this formula is a precaution against
under-estimation of this delay due to for example to delay
jitter or ack compression.

We use the observed rate to compute a "corrected" value of the
bytes acked;

~~~
corrected_bytes_acked = observed_rate*min_rtt
~~~
We then use these values to update the nominal rate,
nominal CWND, max CWND and max bytes acked state variables:

~~~
nominal_rate = max(observed_rate, nominal_rate)
nominal_CWND = max(corrected_bytes_acked, nominal_CWND)
max_CWND = max(nominal_CWND, max_CWND)
max_bytes_acked = max(bytes_acked,max_bytes_acked)
~~~
This strategy is effectively a form of "make before break".
The pushing
only increase the rate and CWND by a fraction of the nominal values,
and only lasts for one round trip. That limited increase is not
expected to increase the size of queues by more than a small
fraction of the bandwidth\*delay product. It might cause a
slight increase of the measured RTT for a short period, or
perhaps cause some ECN signalling, but it should not cause packet
losses -- unless C4 is in "pig war" mode. If there was no extra
capacity available, C4 does not increase the nominal CWND and
the connection continues with the previous value.

## Driving for fairness {#fairness}

TODO: move to appendix, unproven. Consider that Max RTT is in fact
a shared variable, implicitly tracked by Cubic.

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

We could also compute these fractions based on the measured bandwidth, "nominal CWND"
divided by "target RTT", which would make fairness less dependent on path RTT. we did
not, because TCP RENO's mechanism are strictly based on CWND size, and thus fairness
requires using the same criteria.

## Cascade of Increases {#cascade}

We sometimes encounter networks in which the available bandwidth changes rapidly.
For example, when a competing connection stops, the available capacity may double.
With low Earth orbit satellite constellations (LEO), it appears
that ground stations constantly check availability of nearby satellites, and
switch to a different satellite every 10 or 15 seconds depending on the
constellation (see {{ICCRG-LEO}}), with the bandwidth jumping from 10Mbps to
65Mbps.

Because we aim for fairness with RENO or Cubic, the cycle of recovery, cruising
and pushing will only result in slow increases increases, maybe 25% after 10 RTT.
This means we would only double the bandwidth after about 40 RTT, or increase
from 10 to 65 Mbps after almost 200 RTT -- by which time the LEO station might
have connected to a different orbiting satellite. To go faster, we implement
a "cascade": if three successive pushings all result in increases of the
nominal rate, C4 will reenter the "startup" mode, during which each RTT
can result in a 100% increase of rate and CWND.

# Supporting Application Limited Connections {#limited}

C4 is specially designed to support multimedia applications,
which very often operate in application limited mode.
After testing and simulations of application limited applications,
we incorporated a number of features.

The first feature is the design decision to only lower the nominal
rate and CWND
if congestion is detected. This is in contrast with the BBR design,
in which the estimate of bottleneck bandwidth is also lowered
if the bandwidth measured after a "probe bandwidth" attempt is
lower than the current estimate while the connection was not
"application limited". We found that detection of the application
limited state was somewhat error prone. Occasional errors end up
with a spurious reduction of the estimate of the bottleneck bandwidth.
These errors can accumulate over time, causing the bandwidth
estimate to "drift down", and the multimedia experience to suffer.
Our strategy of only reducing the nominal values in
reaction to congestion notifications much reduces that risk.

The second feature is the "make before break" nature of the rate and CWND
updates discussed in {{cwnd-update}}. This reduces the risk
of using rate and CWND that are too large and would cause queues or losses,
and thus make C4 a good choice for multimedia applications.

C4 adds two more features to handle multimedia
applications well: coordinated pushing (see {{coordinated-pushing}}),
and variable pushing rate (see {{variable-pushing}}).

## Coordinated Pushing {#coordinated-pushing}

As stated in {{fairness}}, the connection will remain in "cruising"
state for a specified interval, and then move to "pushing". This works well
when the connection is almost saturating the network path, but not so
well for a media application that uses little bandwidth most of the
time, and only needs more bandwidth when it is refreshing the state
of the media encoders and sending new "reference" frames. If that
happens, pushing will only be effective if the pushing interval
coincides with the sending of these reference frames. If pushing
happens during an application limited period, there will be no data to
push with and thus no chance of increasing the nominal rate and CWND.
If the reference frames are sent outside of a pushing interval, the
rate and CWND will be kept at the nominal value.

To break that issue, one could imagine sending "filler" traffic during
the pushing periods. We tried that in simulations, and the drawback became
obvious. The filler traffic would sometimes cause queues and packet
losses, which degrade the quality of the multimedia experience.
We could reduce this risk of packet losses by sending redundant traffic,
for example creating the additional traffic using a forward error
correction (FEC) algorithm, so that individual packet losses are
immediately corrected. However, this is complicated, and FEC does
not always protect against long batches of losses.

C4 uses a simpler solution. If the time has come to enter pushing, it
will check whether the connection is "application limited", which is
simply defined as testing whether the application send a "nominal CWND"
worth of data during the previous interval. If it is, C4 will remain
in cruising state until the application finally sends more data, and
will only enter the the pushing state when the last period was
not application limited.

## Variable Pushing Rate {#variable-pushing}

C4 tests for available bandwidth at regular pushing intervals
(see {{fairness}}), during which the rate and CWND is set at 25% more
than the nominal values. This mimics what BBR
is doing, but may be less than ideal for real time applications.
When in pushing state, the application is allowed to send
more data than the nominal CWND, which causes temporary queues
and degrades the experience somewhat. On the other hand, not pushing
at all would not be a good option, because the connection could
end up stuck using only a fraction of the available
capacity. We thus have to find a compromise between operating at
low capacity and risking building queues. 

We manage that compromise by adopting a variable pushing rate:

- If pushing at 25% did not result in a significant increase of
  the nominal rate, the next pushing will happen at 6.25%
- If pushing at 6.25% did result in some increase of the nominal CWIN,
  the next pushing will happen at 25%, otherwise it will
  remain at 6.25%

As explained in {{cascade}}, if three consecutive pushing attempts
result in significant increases, C4 detects that the underlying network
conditions have changed, and will reenter the startup state.

The "significant increase" mentioned above is a matter of debate.
Even if capacity is available,
increasing the send rate by 25% does not always result in a 25%
increase of the acknowledged rate. Delay jitter, for example,
may result in lower measurement. We initially computed the threshold
for detecting "significant" increase as 1/2 of the increase in
the sending rate, but multiple simulation shows that was too high and
and caused lower performance. We now set that threshold to 1/4 of the
increase in he sending rate.

## Pushing rate and Cascades

The choice of a 25% push rate was motivated by discussions of
BBR design. Pushing has two parallel functions: discover the available
capacity, if any; and also, push back against other connections
in case of competition. Consider for example competition with Cubic.
The Cubic connection will only back off if it observes packet losses,
which typically happen when the bottleneck buffers are full. Pushing
at a high rate increases the chance of building queues,
overfilling the buffers, causing losses, and thus causing Cubic to back off.
Pushing at a lower rate like 6.25% would not have that effect, and C4
would keep using a lower share of the network. This is why we will always
push at 25% in the "pig war" mode.

The computation of the interval between pushes is tied to the need to
compete nicely, and follows the general idea that
the average growth rate should mimic that of RENO or Cubic in the
same circumstances. If we pick a lower push rate, such as 6.25% or
maybe 12.5%, we might be able to use shorter intervals. This could be
a nice compromise: in normal operation, push frequently, but at a
low rate. This would not create large queues or disturb competing
connections, but it will let C4 discover capacity more quickly. Then,
we could use the "cascade" algorithm to push at a higher rate,
and then maybe switch to startup mode if a lot of capacity is
available. This is something that we intend to test, but have not
implemented yet.

# State Machine

TODO: we do not need the slowdown and checking states.

The state machine for C4 has the following states:

* "startup": the initial state, during which the CWND is
  set to twice the "nominal_CWND". The connection
  exits startup if the "nominal_cwnd" does not
  increase for 3 consecutive round trips. When the
  connection exits startup, it enters "recovery".
* "recovery": the connection enters that state after
  "startup", "pushing", or a congestion detection in
  a "cruising" state. It remains in that state for
  at least one roundtrip, until the first packet sent
  in "discovery" is acknowledged. Once that happens,
  the connection enters "slowdown" if no "slowdown"
  has been experienced for 5 seconds, or goes back
  to "startup" if the last 3 pushing attemps have resulted
  in increases of "nominal CWND", or enters "cruising"
  otherwise.
* "cruising": the connection is sending using the
  "nominal_rate" and "nominal_cwnd" value. If congestion is detected,
  the connection exits cruising and enters
  "recovery" after lowering the value of
  "nominal_cwnd". If after a roundtrip the application
  sent fewer than 1/2 of "nominal_cwnd", and if the
  last "slowdown" occured more than 4 seconds ago,
  the connection moves to the "checking" state.
  Otherwise, the connection will
  remain in "cruising" state until a sufficient
  number of bytes have been acknowledged, and
  the connection is not "app limited". At that
  point, it enters "pushing".
* "pushing": the connection is using a rate and CWND 25%
  larger than "nominal_rate" and "nominal_CWND".
  It remains in that state
  for one round trip, i.e., until the first packet
  send while "pushing" is acknowledged. At that point,
  it enters the "recovery" state. 
* "slowdown": the connection is using rate and CWND set to
  1/2 of the nominal values. After one round trip, it exits "slowdown"
  and enters "checking".
* "checking": after a forced or natural slowdown,
  the connection enters the checking state. It is
  sending using rate and CWND set to the nominal values.
  The connection remains in
  checking state for one round trip. After that round trip,
  it goes back to "cruising" if the lowest RTT measured
  during the round trip or the previous checking state is lower
  than or equal to the
  current min RTT. If not, if the lowest RTT measured during
  two consecutive slowdown periods is higher than the min RTT,
  it resets the min RTT to the last measurement and reenters
  "startup".

These transitions are summarized in the following state
diagram.

~~~
                    Start
                      |
                      v
                      +<-----------------------+-------------------+
                      |                        |                   ^
                      v                        |                   |
                 +----------+                  |                   |
                 | Startup  |                  |                   |
                 +----|-----+                  |                   |
                      |                        |                   |
                      v                        |                   |
                 +---------------+             |                   |
  +--+---------->|   Recovery    |             |                   |
  ^  ^           +----|---|---|--+             |                   |
  |  |                |   |   | Rapid Increase |                   |
  |  |                |   |   +--------------->+                   |
  |  |                |   |                                        |
  |  |                |   v   Forced Slowdown                      |
  |  |                |   +------------------>+                    |
  |  |                |                       |                    |
  |  |                +<----------------------|------------------+ |
  |  |                |                       |                  ^ |
  |  |                v                       v                  | |
  |  |           +----------+            +----------+            | |
  |  |           | Cruising |            | Slowdown |            | |
  |  |           +-|--|--|--+            +----|-----+            | |
  |  | Congestion  |  |  |  Natural Slowdown  |                  | |
  |  +-------------+  |  +------------------->+                  | |
  |                   |                       |                  | |
  |                   v                       v                  | |
  |              +----------+            +-----------+           | |
  |              | Pushing  |            | Checking  |           | |
  |              +----|-----+            +---|---|---+           | |
  |                   |                      |   | RTT Min lower | |
  +<------------------+                      |   +-------------->+ |
                                             |                     |
                                             | RTT Min Higher      |
                                             +-------------------->+
~~~
  

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







