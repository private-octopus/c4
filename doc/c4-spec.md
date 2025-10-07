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
- number of eras without increase (see {{c4-initial}}),
- number of successful pushes,
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
alpha_current; /* coefficient alpha used in the current state */
alpha_previous; /* coefficient alpha used in the previous era */
era_max_rtt; /* max RTT observed during this era */
era_min_rtt; /* min RTT observed during this era */
~~~
These variables are initialized at the beginning of the era.


## Nominal rate {#nominal-rate}

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

The nominal rate is reduced following congestion events,
as specified in {{congestion-response}}.

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

C4's goal is to obtain a estimate of the combination of path latency
and maximum jitter. This is done by only taking measurements
when C4 is sending data at a rate below the nominal transmission rate,
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

The state machine for C4 has the following states:

* "Initial": the initial state, during which the CWND is
  set to twice the "nominal_CWND". The connection
  exits startup if the "nominal_cwnd" does not
  increase for 3 consecutive round trips. When the
  connection exits startup, it enters "recovery".
* "Recovery": the connection enters that state after
  "Initial", "pushing", or a congestion detection in
  a "cruising" state. It remains in that state for
  at least one roundtrip, until the first packet sent
  in "recovery" is acknowledged. Once that happens,
  the connection goes back
  to "startup" if the last 3 pushing attemps have resulted
  in increases of "nominal rate", or enters "cruising"
  otherwise.
* "Cruising": the connection is sending using the
  "nominal_rate" and "nominal_max_rtt" value. If congestion is detected,
  the connection exits cruising and enters
  "recovery" after lowering the value of
  "nominal_cwnd".
  Otherwise, the connection will
  remain in "cruising" state until at least 4 RTT and
  the connection is not "app limited". At that
  point, it enters "pushing".
* "Pushing": the connection is using a rate and CWND 25%
  larger than "nominal_rate" and "nominal_CWND".
  It remains in that state
  for one round trip, i.e., until the first packet
  send while "pushing" is acknowledged. At that point,
  it enters the "recovery" state.

These transitions are summarized in the following state
diagram.

~~~
                    Start
                      |
                      v
                      +<-----------------------+
                      |                        |
                      v                        |
                 +----------+                  |
                 | Startup  |                  |
                 +----|-----+                  |
                      |                        |
                      v                        |
                 +------------+                |
  +--+---------->|  Recovery  |                |
  ^  ^           +----|---|---+                |
  |  |                |   |     Rapid Increase |
  |  |                |   +------------------->+
  |  |                |
  |  |                v
  |  |           +----------+
  |  |           | Cruising |
  |  |           +-|--|-----+
  |  | Congestion  |  |
  |  +-------------+  |
  |                   |
  |                   v
  |              +----------+
  |              | Pushing  |
  |              +----|-----+
  |                   |
  +<------------------+

~~~

## Setting pacing rate, congestion window and quantum

If the nominal rate or the nominal max RTT are not yet
assessed, C4 sets pacing rate, congestion window and
pacing quantum to initial values:

* pacing rate: set to the data rate of the outgoing interface,
* congestion window: set to the equivalent of 10 packets,
* congestion quantum: set to zero.

If the nominal rate or the nominal max RTT are both
assessed, C4 sets pacing rate, and congestion window 
to values that depends on these variables
and on a coefficient `alpha_current`:

~~~
pacing_rate = alpha_current_ * nominal_rate
cwnd = max (pacing_rate * nominal_max_rtt, 2*MTU)
quantum = max ( min (cwnd / 4, 64KB), 2*MTU)
~~~

The coefficient `alpha` for the different states is:

state | alpha | comments
------|-------|----------
Initial | 2 |
Recovery | 15/16 |
Cruising | 1 |
Pushing | 5/4 or 17/16 | see {{c4-pushing}} for rules on choosing 5/4 or 17/16

## Initial state {#c4-initial}

When the flow is initialized, it enters the Initial state,
during which it does a first assessment of the 
The coefficient `alpha_current` is set to 2. The
"nominal rate" and "nominal max RTT" are initialized to zero,
which will cause setting pacing rate and CWND to default
initial values. The nominal max RTT will be set to the
first assessed RTT value, but is not otherwise changed
during the initial phase. The nominal rate is updated
after receiving acknowledgements, per {#nominal-rate}.

C4 will exit the Initial state and enter Recovery if the 
nominal rate does not increase for 3 consecutive eras,
omitting the eras for which the transmission was
"application limited".

C4 will only react to congestion events during the
Initial phase if the nominal rate did not increase
during the previous era. In that case, C4 will
exit the Initial state and enter Recovery.

## Recovery state {#c4-recovery}

The recovery state is entered from the Initial or Pushing state,
or from the Cruising state in case of congestion. 
The coefficient `alpha_current` is set to 15/16. Because the multiplier
is lower than 1, the new value of CWND may well be lower
than the current number of bytes in transit. C4 will wait
until acknowledgements are received and the number of bytes
in transit is lower than CWND to send new packets.

The Recovery ends when the first packet sent during that state
is acknowledged. That means that acknowledgement and congestion
signals received during recovery are the consequence of packets
sent before. C4 assumes that whatever corrective action is required
by these events will be taken prior to entering recovery, and that
events arriving during recovery are duplicate of the prior events
and can be ignored.

Rate increases are detected when acknowledgements received during recovery
refect a successful "push" during the Pushing phase. The prior "Pushing"
is considered successful if it did not trigger any congestion event,
and if the data rate increases sufficiently
between the end of previous Recovery and the end of this one, with
sufficiently being defined as:

* Any increase if the prior pushing rate (alpha_prior) was 17/16 or
less,
* An increase of at least 1/4th of the expected increase otherwise,
for example an increase of 1/16th is `alpha_previous` was 5/4.

C4 re-enters "Initial" at the end of the recovery period if the evaluation
shows 3 successive rate increases without congestion. Otherwise,
C4 enters cruising.

## Cruising state {#c4-cruising }

# Handling of congestion signals {#congestion-response}

C4 responds to congestion events by reducing the nominal rate, and
in some condition also reducing the nominal max RTT. C4 monitors
3 types of congestion events:

1. Excessive increase of measured RTT (except in pig war mode),
2. Excessive rate of packet losses (but not mere Probe Time Out, see {{no-pto}}),
3. Excessive rate of ECN/CE marks

If any of these signals is detected, C4 enters a "recovery"
state. 

## Rate Reduction on Congestion

On entering recovery, C4 reduces the `nominal_rate` by a factor "beta":
~~~
    nominal_rate = (1-beta)*nominal_rate
~~~
The coefficient `beta` differs depending on the nature of the congestion
signal. For packet losses, it is set to `1/4`, similar to the
value used in Cubic. For delay based losses, it is proportional to the
difference between the measured RTT and the target RTT divided by
the acceptable margin, capped to `1/4`. If the signal
is an ECN/CE rate, we may
use a proportional reduction coefficient in line with
{{RFC9331}}, again capped to `1/4`.

## Do not react to Probe Time Out {#no-pto}

QUIC normally detect losses by observing gaps in the sequences of acknowledged
packet. That's a robust signal. QUIC will also inject "Probe time out"
packets if the PTO timeout elapses before the last sent packet has not been acknowledged.
This is not a robust congestion signal, because delay jitter may also cause
PTO timeouts. When testing in "high jitter" conditions, we realized that we should
not change the state of C4 for losses detected solely based on timer, and
only react to those losses that are detected by gaps in acknowledgements.

## Driving for fairness {#fairness}

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







