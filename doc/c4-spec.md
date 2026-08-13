---
title: "Specification of Christian's Congestion Control Code (C4)"
abbrev: "C4 Specification"
category: exp

docname: draft-huitema-ccwg-c4-spec-latest
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
   RFC2119:
   RFC8174:
informative:
   RFC6582:
   RFC9000:
   I-D.ietf-moq-transport:
   RFC9331:
   RFC9406:
   RFC9959:

--- abstract

Christian's Congestion Control Code is a new congestion control
algorithm designed to support Real-Time applications such as
Media over QUIC. It is designed to drive towards low delays,
with good support for the "application limited" behavior
frequently found when using variable rate encoding, and
with fast reaction to congestion to avoid the "priority
inversion" happening when congestion control overestimates
the available capacity. The design emphasizes simplicity and
avoids making too many assumptions about the "model" of
the network.

--- middle

# Introduction

Christian's Congestion Control Code (C4) is a congestion control
algorithm designed to support Real-Time multimedia applications, specifically
multimedia applications using QUIC {{RFC9000}} and the Media
over QUIC transport {{I-D.ietf-moq-transport}}.

The two main variables describing the state of a flow are the
"nominal rate" (see {{nominal-rate}}) and the
"nominal max RTT" (see {{nominal-max-rtt}}).
C4 organizes the management of the flow through a series of
states: Initial, during which the first assessment of nominal-rate
and nominal max RTT are obtained, Resuming for the implementation
of careful resume {{RFC9959}}, Recovery in which a flow is
stabilized after the Initial, Probing or Pushing phase, Cruising during which
a flow uses the nominal rate, Probing during which the flow
tries to discover whether more resource mighht be available and Pushing during which the flow
tries to otain more resource  -- see {{c4-states}}.

C4 divides the duration of the connection in a set of "eras",
each corresponding to a packet round trip. Transitions between protocol
states typically happen at the end of an era, except if the
transition is forced by a congestion event.

C4 assumes that the transport stack is
capable of signaling events such
as acknowledgements, RTT measurements, ECN signals or the detection
of packet losses. It also assumes that the congestion algorithm
controls the transport stack by setting the congestion window
(CWND) and the pacing rate (see {{congestion-response}}).

C4 introduces the concept of "sensitivity" (see {{sensitivity}})
to ensure that flows using a large amount of bandwidth are more
"sensitive" to congestion signals than flows using fewer bandwidth,
and thus that multiple flows sharing a common bottleneck are driven
to share the resource evenly.


# Key Words

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL
NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "NOT RECOMMENDED",
"MAY", and "OPTIONAL" in this document are to be interpreted as
described in BCP 14 {{RFC2119}} {{RFC8174}} when, and only when, they
appear in all capitals, as shown here.

# C4 variables

In addition to the nomnal rate and the nominal max RTT,
C4 maintains a set a variables per flow (see {{global-variables}})
and per era (see {{era-variables}}).

## Nominal rate {#nominal-rate}

The nominal rate is an estimate of the bandwidth available to the flow.
On initialization, the nominal rate is set to zero, and default values
are used when setting the pacing rate and CWND for the flow.

C4 evaluates the nominal rate after acknowledgements are received
using the number of bytes acknowledged since the packet was sent
(`bytes_acknowledged`) and the time delay it took to process these packets.

That delay is normally set to the difference between the time
at which the acknowledged packet was sent (`time_sent`),
and the current time (`current_time`). However, that difference
may sometimes be severely underestimated because of delay jitter
and ACK compression. We also compute a "send delay" as the difference
between the send time of the acknowledged packet and the send time
of the oldest "delivered" packet. 

~~~
delay_estimate = max (current_time - time_sent, send_delay)
rate_estimate = bytes_acknowledged /delay_estimate
~~~

If we are not in a congestion situation, we update the
nominal rate:

~~~
if not congested and nominal_rate > rate_estimate:
    nominal_rate = rate_estimate
~~~

The data rate measurements can only cause increases in
the nominal rate. The nominal rate is reduced following
congestion events, as specified in {{congestion-response}}.

The "congested" condition is defined as being in the
recovery state and having either entered that state due
to a congestion event, or having received a congestion
event after entering recovery. 

Updating the nominal rate
in these conditions would cause a congestion bounce: the
nominal rate is reduced because of a congestion event,
C4 enters recovery, but then packets sent at the previous
rate are received during recovery, generating a new estimate
and resetting the nominal rate to a value close to the one
that caused congestion.


## Nominal max RTT {#nominal-max-rtt}

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
when C4 is sending data at a rate not higher than the nominal transmission rate,
as happens for example in the recovery and cruising states. These measurements
will happen during the following era. C4 captures them
by recording the max RTT for packets sent in that era.
C4 will also progressively reduce the value of the
nominal max RTT over time, to account for changes in network
conditions.

~~~
# on end of era

if alpha_previous <= 1.0:
    if era_min_rtt < running_min_rtt:
        running_min_rtt = era_min_rtt
    else:
        running_min_rtt =
           (7*running_min_rtt + era_min_rtt)/8

    if era_max_rtt > running_min_rtt + MAX_JITTER:
        # cap RTT increases to MAX_JITTER, i.e., 250ms
        era_max_rtt = running_min_rtt + MAX_JITTER
    if era_max_rtt > nominal_max_rtt:
        nominal_max_rtt = era_max_rtt
    else:
        nominal_max_rtt =
          (7*nominal_max_rtt + era_max_rtt)/8
~~~

The decrease over time is tuned so that jitter
events will be remembered for several of the
cruising-pushing-recovery cycles, which is enough time for the
next jitter event to happen, at least on Wi-Fi networks.

## Global variables {#global-variables}

In addition to the nominal rate and nominal MAX RTT,
C4 maintains a set of variables tracking the evolution of the flow:


- current state of the algorithm, which can be Initial, Resuming, Recovery,
  Cruising, Probing or Pushing.
- running min RTT, an approximation of the min RTT for the flow,
- number of eras without increase (see {{c4-initial}}),
- the number of successive congestion events and the recent maximum rate,
  used to detect and manage persistent congestion (see {{persistent-congestion}}).


## Per era variables {#era-variables}

C4 keeps variables per era:

~~~
era_sequence; /* sequence number of first packet sent in this era */
alpha_current; /* coefficient alpha used in the current state */
alpha_previous; /* coefficient alpha used in the previous era */
era_max_rtt; /* max RTT observed during this era */
era_min_rtt; /* min RTT observed during this era */
~~~

These variables are initialized at the beginning of the era.

# States and Transition {#c4-states}


The state machine for C4 has the following states:

* "startup": the initial state, during which the CWND is
  set to twice the "nominal_CWND". The connection
  exits startup if the "nominal_cwnd" does not
  increase for 3 consecutive round trips. When the
  connection exits startup, it enters "recovery".
* "resuming": management of careful resume, during which the CWND and pacing rate are
  pegged to the seed values. The state lasts for 2 eras,
  giving enough time for the rate measurement to stabilize.
  The eras are expanded if the connection is app limited,
  to avoid exiting too early. After two eras, or at any time
  if congestion is detected, the state transitions to recovery.
* "recovery": the connection enters that state after
  "startup", "pushing", or a congestion detection in
  a "cruising" state. It remains in that state for
  at least one roundtrip, until the first packet sent
  in "recovery" is acknowledged. Once that happens,
  the connection goes back
  to "startup" if the last 3 pushing attemps have resulted
  in increases of "nominal rate", or enters "cruising"
  otherwise.
* "cruising": the connection is sending using the
  "nominal_rate" and "nominal_max_rtt" value. If congestion is detected,
  the connection exits cruising and enters
  "recovery" after lowering the value of
  "nominal_cwnd".
  Otherwise, the connection will
  remain in "cruising" state until at least 4 RTT and
  the connection is not "app limited". At that
  point, it enters "pushing".
* "probing": the connection is using a rate and CWND 6.25%
  larger than "nominal_rate" and "nominal_CWND", or 3.125%
  if the local gateway is ECN capable. After
  1 RTT, it moves back to "recovery" in order to assess
  the results. If the data rate appears to have increased,
  the connection moves to the "pushing" state.
* "pushing": the connection is using a rate and CWND 25%
  larger than "nominal_rate" and "nominal_CWND".
  It remains in that state for at least one round trip,
  and until the measured rate stops growing. If the
  pushing lasts more than 3 RTT, C4 re-enters the
  initial state.

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
                 +-|--|-----+                  |
         +---------+  |                        | 
         |            |                        |
         v            |                        |
   +----------+       |                        |
   | Resuming |       |                        |
   +-----|----+       |                        |
         +---------+  |                        |
                   |  |                        |
                   v  v                        |
                 +------------+                |
  +--+---------->|  Recovery  |                |
  ^  ^           +----|---|---+                |
  |  |                |   | Rate increase      |
  |  |                |   +---------+          |
  |  |                |             |          |
  |  |                v             |          |
  |  |           +----------+       |          |
  |  |           | Cruising |       |          |
  |  |           +-|--|-----+       v          |
  |  | Congestion  |  |        +---------+     |
  |  +-------------+  |        | Pushing |     |
  |                   |        +----|--|-+     |
  |                   v             |  |       |
  |              +----------+       |  +-------+
  |              | Probing  |       |   Rapid
  |              +----|-----+       |   increase
  |                   |             |
  +<------------------+             |
  ^                                 |
  |                                 |
  +---------------------------------+

~~~

## Setting pacing rate, congestion window and quantum {#set_pace}

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
pacing_rate = alpha_current * nominal_rate

if (c4_state == initial):
    margin = 0
else:
    margin = min(nominal_max_rtt/4, 15_milliseconds)

cwnd = max ((pacing_rate+margin) * nominal_max_rtt, 2*MTU)
~~~

During the initial phase, the pacing rate is set to a minimum
of 1,048,576 bps (128 KB/s) to avoid starting at too low a rate.
In these conditions, the transmission is expected to be limited
by the value of CWND.

~~~
if (c4_state == initial and pacing_rate < 1,048,576 bps):
    pacing_rate = 1,048,576 bps
~~~

The "margin" coefficient accounts for errors on the
estimate of the nominal max rtt, which could cause C4
to be stuck operating at a too low data rate. It is only
applied outside of the initial phase.

The coefficient `alpha` for the different states is:

state | alpha | comments
------|-------|----------
Initial | 2 | See {{c4-initial}} for the setting of CWND
Resuming | variable | pacing and CWND are set from remembered values
Recovery | 15/16 or 3/4 | (set to 3/4 if draining is required, see {{draining}})
Cruising | 1 |
Probing | 33/32 or 17/16 | see {{c4-probing}} for rules on choosing 33/32 or 17/16
Pushing | 5/4 |

Setting the pacing quantum is a tradeoff between two requirements.
Using a large quantum enables applications to send large batches of
packets in a single transaction, which improves performance. But
sending large batches of packets creates "instant queues" and
causes some Active Queue Management mechanisms to mark packets as
ECN/CE, or drop them. As a compromise, we set the quantum to
4 milliseconds worth of transmission, while capping it to 64KB.

~~~
quantum = max ( min (pacing_rate*4_milliseconds, 64KB), 2*MTU)
~~~

## Initial state {#c4-initial}

When the flow is first initialized, it enters the Initial state,
during which it does a first assessment of the
"nominal rate" and "nominal max RTT".
The coefficient `alpha_current` is set to 2. The
"nominal rate" and "nominal max RTT" are initialized to zero,
which will cause pacing rate to be set to a default
initial value. The nominal max RTT will be set to the
first assessed RTT value, but is not otherwise changed
before the end of the initial phase.

During the initial state, the nominal rate is updated
after receiving acknowledgements, see {{nominal-rate}}.
The pacing rate is set to the maximum of the nominal rate
and a minimum of 1,048,576 bps (128 KB/s) to avoid starting
at too low a rate.
C4 will exit the Initial state and enter Recovery if the 
nominal rate does not increase for 3 consecutive eras,
omitting the eras for which the transmission was
"application limited".

During the initial state, C4 maintains an "initial_cwnd" variable,
which is initialized to the value of CWND at the beginning of the
initial state, and is incremented aftre each acknowledgement
using the formula:

initial_cwnd += nb_bytes_acknowledged >> (3 * nb_eras_no_increase)

As long as the `nominal_rate` increases, the `nb_eras_no_increase`
will be zero, and the increment of `initial_cwnd` will mimic those
of TCP Reno {{RFC6582}}. When the `nominal_rate` stops increasing, the increment
will get progressively slower, similar to the reduced increment
of Hystart++ {{RFC9406}}.

The CWND variable will be set to the maximum of the initial_cwnd
and the product of the nominal rate times the nominal max RTT times
`alpha_current`.

C4 exit the Initial when receiving a congestion signal if the
following conditions are true:

1- If the signal is due to "delay" or "ECN", C4 will only exit the
   initial state if the `nominal_rate` did not increase
   in the last 2 eras.

2- If the signal is due to "loss", C4 will only exit the
   initial state if more than 20 packets have been received.

The restriction on delay signals and ECN is meant to prevent spurious exit
due to delay jitter or competing connections. The restriction on loss
signals is meant to ensure that enough packets have been received to properly
assess the loss rate.

On exiting the Initial state, C4 computes an estimate of the nominal
max RTT as the quotient of the half the last CWND divided by the last
nominal rate, and updates the "nominal max RTT" accordingly.

### Reentering the initial state

When reentering the initial state, C4 already has an estimate of the
current nominal rate and nominal max RTT. CWND is set to the product of
nominal rate and nominal max RTT. The initial state then operates as
specified in {{c4-initial}}.

## Resuming state

The resuming state is entered if the application remembers the CWND and RTT
of a previous connection between the same endpoints. The resuming state lasts for 2 eras,
during which the CWND and pacing rate are pegged to the remembered values.
The first of these eras can be extended if the connection is "application limited",
to avoid exiting too early. After 2 eras, or if a congestion signal is received before
that, C4 enters recovery.

## Recovery state {#c4-recovery}

The recovery state is entered from the Initial, Resuming, Probing or Pushing state,
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

Rate increases are detected if the previous state was Probing, 
and if acknowledgements received during recovery
reflect a successful "probe" during the Probing phase, that is if the
probing did not trigger any congestion event
and if the data rate did increase.

If a succesful probing was detected, C4 immediately enters the Pushing state.

C4 re-enters "Initial" at the end of the recovery period if 
high jitter requires restarting the Initial phase (see
{{restart-high-jitter}}). Otherwise, C4 enters cruising.

Receiving of a congestion signal during the Initial phase does not
cause a change in the `nominal_rate` or `nominal_max_RTT`.

### Restarting Initial if High Jitter {#restart-high-jitter}

The "nominal max RTT" is not updated during the Initial phase,
because doing so would prevent exiting Initial on high delay
detection. This can lead to underestimation of the "nominal
rate" if the flow is operating on a path with high jitter.

C4 will reenter the "initial" phase if
high jitter is detected for the flow. The high jitter
is detected after updating the "nominal max RTT" at the
end of the recovery era, if:

~~~
running_min_rtt < nominal_max_rtt*2/5
~~~

## Cruising state {#c4-cruising }

The Cruising state is entered from the Recovery state. 
The coefficient `alpha_current` is set to 1.

C4 will transition from Cruising state to Probing state
after 2 eras.

C4 will transition to Recovery before that if
a congestion signal is received before transition to Probing.

## Probing state {#c4-probing}

The probing state is entered from the Cruising state.

The coefficient `alpha_current` is set to 17/16, unless ECN-CE
marks have been received on the path, in which case it is set to 33/32.
The presence of ECN/CE means that an
on path router is implementing either L4S ({{RFC9331}}) or another
ECN marking scheme.

C4 exits the probing state after one era, or if a congestion
signal is received before that.

## Pushing state {#c4-pushing}

The pushing state is entered from the Recovery state if a previous
probing was successful, as stated in {{c4-recovery}}. 

The coefficient `alpha-current` is set to 5/4.

The pushing phase lasts for at least two eras. During the first
era, measurements correspond to data sent during the recovery
phase, which are unlikely to result in detection of rate increases.
After that first phase, C4 assesses whether the new "nominal rate"
has increased sufficiently druing the previous RTT. If it has, C4
will continue in the pushing phase. If it has not, the flow will
transition to recovery.

We define "increased sufficiently" as reaching at least 19/16th of the
nominal rate at the beginning of the era.

C4 also exits the pushing state if a congestion
signal is received. In an exception to
standard congestion processing, the reduction in `nominal_rate` and
`nominal_max_RTT` are not applied if the congestion signal
is tied to a packet sent during the Pushing state. 

# Handling of congestion signals {#congestion-response}

C4 responds to congestion events by reducing the nominal rate, and
in some condition also reducing the nominal max RTT. C4 monitors
3 types of congestion events:

1. Excessive increase of measured RTT,
2. Excessive rate of packet losses (but not mere Probe Time Out, see {{no-pto}}),
3. Excessive rate of ECN/CE marks

C4 monitors successive RTT measurements and compare them to
a reference value, defined as the sum of the "nominal max rtt"
and a "delay threshold". C4 monitors the arrival of packet losses
computes a "smoothed error rate", and compares it to a
"loss threshold". When the path supports ECN, C4 monitors the
arrival of ECN marks and computes a "smoothed CE rate",
and compares it to a "CE threshold". These coefficients
depend on the sensitivity coefficient defined in {{sensitivity}}.

## Variable Sensitivity {#sensitivity}

The three congestion detection thresholds are
function of the "sensitivity" coefficient,
which increases with the nominal rate of the flow. Flows
operating at a low data rate have a low sensitivity coefficient
and reacts slower to congestion signals than flows operating
at a higher rate. If multiple flows share the same bottleneck,
the flows with higher data rate will detect congestion signals
and back off faster than flow operating at lower rate. This will
drive these flows towards sharing the available resource evenly.

The sensitivity coefficient varies from 0 to 1, according to
a simple curve:

* set sensitivity to 0 if data rate is lower than 50000 B/s
* linear interpolation between 0 and 0.92 for values
  between 50,000 and 1,000,000 B/s.
* linear interpolation between 0.92 and 1 for values
  between 1,000,000 and 10,000,000 B/s.
* set sensitivity to 1 if data rate is higher than
  10,000,000 B/s

The sensitivity index is then used to set the value of delay and
loss and CE thresholds.

## Detecting Excessive Delays

The delay threshold is function of the nominal max RTT and the
sensitivity coefficient:

~~~
    delay_fraction = 1/16 + (1 - sensitivity)*3/16
    delay_threshold = min(25ms, delay_fraction*nominal_max_rtt)
~~~

A delay congestion signal is detected if:

~~~
    rtt_sample > nominal_max_rtt + delay_threshold
~~~

## Detecting Excessive Losses

C4 maintains an average loss rate, updated for every packet
as:

~~~
    if packet_is_lost:
        loss = 1
    else:
        loss = 0
    smoothed_loss_rate = (loss + 15*smoothed_loss_rate)/16
~~~

The loss threshold is computed as:

~~~
    loss_threshold = 0.02 + 0.50 * (1-sensitivity);
~~~

A loss is detected if the smoothed loss rate is larger than the threshold.
In that case, the coefficient `beta` is set to 1/4.

### Do not react to Probe Time Out {#no-pto}

QUIC normally detect losses by observing gaps in the sequences of acknowledged
packet. That's a robust signal. QUIC will also inject "Probe time out"
packets if the PTO timeout elapses before the last sent packet has not been acknowledged.
This is not a robust congestion signal, because delay jitter may also cause
PTO timeouts. When testing in "high jitter" conditions, we realized that we should
not change the state of C4 for losses detected solely based on timer, and
only react to those losses that are detected by gaps in acknowledgements.

## Detecting Excessive CE Marks {#process-ecn}

The way we handle ECN signals is designed to be compatible with L4S {{RFC9331}}.
When the path supports ECN marking, C4 monitors the arrival of ECN/CE and
ECN/ECT(1) marks by computing the ratio `ecn_alpha`. Congestion is detected
when that ratio exceeds `ecn_threshold`, which varies depending on the
sensitivity coefficient:

~~~
ecn_threshold = (2-sensitivity)*3/32
~~~

The ratio `ecn_alpha` is
updated each time an acknowledgement is received, as follow:

~~~
delta_ce = increase in the reported CE marks
delta_ect1 = increase in the reported ECT(1) marks
frac = delta_ce / (delta_ce + delta_ect1)

if frac >= 0.5:
    ecn_alpha = frac
else:
    ecn_alpha += (frac - ecn_alpha)/16

if ecn_alpha > ecn_threshold:
    report congestion
~~~

Congestion detection causes C4 to enter recovery. The
ration `ecn_alpha` is set to zero on exit of recovery.

## Applying congestion signals

On congestion signal, if C4 was not in recovery state, it
will enter recovery.

As stated in {{c4-initial}} and {{c4-pushing}}, detecting
a congestion in the Initial or Pushing state does not cause
a change in the `nominal_rate` or `nominal_max_RTT`, because
the pacing rate in these states is larger than the
`nominal_rate`. Rate reduction only happens if recovery
was entered from the Cruising state

### Rate Reduction on Congestion {#rate-reduction}

On entering recovery from the cruising state, C4 reduces the
`nominal_rate` by the factor "beta"
corresponding to the congestion signal:

~~~
    nominal_rate = (1-beta)*nominal_rate
~~~

The coefficient `beta` differs depending on the nature of the congestion
signal. For packet losses, it is set to `1/4`, similar to the
value used in Cubic. 

For delay based losses, it is proportional to the
difference between the measured RTT and the target RTT divided by
the acceptable margin, capped to `1/4`:

~~~
    beta = min(1/4,
              (rtt_sample - (nominal_max_rtt + delay_threshold)/
               delay_threshold))
~~~

If the signal is an ECN/CE rate, the coefficient is proportional
to the difference between `ecn_alpha` and `ecn_threshold`, capped to `1/4`:

~~~
    beta = min(1/4, (ecn_alpha - ecn_threshold)/ ecn_threshold)
~~~

### Reaction to persistent congestion {#persistent-congestion}

C4 makes a distinction between intermittent congestion, which is handled
by reducing the nominal rate as specified in {{rate-reduction}}, and
persistent congestion, which is detected if 2 congestion events
appear in rapid succession.

C4 handles two variables to manage the reaction to persistent congestion:
the number of successive congestion events and the "recent maximum rate":

- The number of successive congestion events is managed upon exiting
  a recovery era. It is reset to zero if no congestion signal was received
  upon entering that era or during that era, and is incremented by 1 otherwise.

- The "recent maximum rate" is the maximum rate measurement observed since
  the end of the previous recovery period, i.e., the recovery period that
  preceded the current one.

If the number of successive congestion events is larger than 1, C4 will
check if at least one rate measurement has been received since the end
of the previous recovery period, i.e, if the "recent maximum rate" is larger
than 0. If so, C4 will reset
the "nominal rate" to the "recent maximum rate".

## Trimming an overestimated nominal rate {#trimming}

Despite taking precautions to make the rate estimation reliable, there
are bound to be mistakes, and the nominal rate may be a little too high.
This will cause the slow building of queues, and a slow increase of
the RTT, until the network queues eventually fill up and a congestion
signal is received.

C4 avoids the slow building of queues by "trimming" the nominal RTT
before exiting the recovery phase, if:

* The nominal data rate did not grow during the previous cycle,
* No congestion was notified, and,
* The connection was not application limited during the pushing phase.

If those conditions are all verified, the "nominal rate" is set to the
average of its current value and the "recent maximum rate".

## Draining the standing queues {#draining}

Queues may build up if C4 has been sending data at a rate higher than the actual path
capacity, and if the number of bytes in transit is higher than the bandwidth
delay product. This can happen during an Initial phase, during a Pushing phase,
or if the path RTT is reduced. When any of these conditions is detected,
C4 sets a "draining needed" flag. Upon entering recovery, if this flag is
set, the coefficient "alpha" is set to 7/8th instead of the default 15/16.


# Implementation considerations

Implementing C4 ought to be straightforward, but developers need to pay
attention to measurement of data rates and to pacing issues when the
CPU load is high.

## Rate measurement should be conservative

The standard algorithm for rate measurement is to consider the amount
of data acknowledged in an interval of time, and divide that amount
by the duration of the interval. This algorithm can result in
over-estimates of the rate in presence of data jitter. These
excessive estimates could cause C4 to set a nominal rate higher
than the network path bandwidth, resulting in queue build-up and
excessive delays.

There are two known ways to reduce the effect of jitter: filter out
measurements in which the data rate measured through acknowledgements
is larger than the send rate; and, make sure that the measurement
interval are long enough so jitter only has a small influence. Cautious
implementations should use both strategies.

## Pacing and CPU load

C4 relies on pacing during to avoid sending data too fast.
Pacing is often implemented
using a "leaky bucket" algorithm, which refills the bucket at the
pacing rate, allows transmission as long as there are enough tokens
in the bucket, and forces transmission to wait when all tokens are
consumed. The wait time is computed based on the pacing rate
and the number of desired tokens, and is implemented using
operating system commands such as `select()`, `poll()`,
`epoll()` or `sleep()`. In high CPU load conditions, we observe
that these commands often return after more than the specified
wait time, resulting in a lower sending rate than the desired
pacing rate.

This phenomenom is particularly visible in low-latency paths.
The generic solution would probably be to estimate how much slower
the actual pacing is compared to the desired rate, and increase the
programmed pacing rate by a value proportional to these measurements.
This generic solution is not yet specified. In between, implementations
had success with a simple fix: increase the pacing rate 3/64th in
"cruising" state when the RTT is less than 1ms. This definitely
improved performance in low-latency environment, in particular
loopback interfaces.

## Nominal max RTT on low latency links

When doing tests on low latency links, we observed on some systems
a lot of measurement jitter. The measured RTT is the sum of the
actual RTT and some system wakeup delay, which can vary between a
few microseconds and maybe 1 millisecond. The default algorithm
will adapt the nominal RTT after each roundtrip, which can lead
to excessively low values, causing a slowdown of the transmission.
A solution is to set a "floor" value to the nominal max RTT,
updating it to the maximum of the measured value and the floor.
Setting the floor value to 1ms did improve performance.

# Security Considerations

We do not believe that C4 introduce new security issues. Or maybe there are,
such as what happen if applications can be fooled in going to fast and
overwhelming the network, or going too slow and underwhelming the application.
Discuss!

# IANA Considerations

This document has no IANA actions.

--- back

# Acknowledgments
{:numbered="false"}

TODO acknowledge.

# Changes since previous versions
{:numbered="false"}

## Changes since draft-huitema-ccwg-c4-spec-04
{:numbered="false"}

This section should be deleted before publication as an RFC

* trimming of the nominal rate if it does not increase for the cycle
* draining of the network queues if the RTT diminishes
* make sure that C4 does not stall in bad wifi conditions

## Changes since draft-huitema-ccwg-c4-spec-03
{:numbered="false"}

Introduce a minimum pacing rate during the initial phase.

## Changes since draft-huitema-ccwg-c4-spec-02
{:numbered="false"}

Added a "resuming" state for implementing the "careful resume"
algorithm.

Separate "probing" state, which lasts just one RTT before
success is evaluated, and a more aggressive "pushing" state,
which lasts until rate measurements stop growing. This replaces
the use of "probe level" introduced in draft-02.

Added a faster "reaction to persistent congestion".

## Changes since draft-huitema-ccwg-c4-spec-01
{:numbered="false"}

Revised the description of the initial state do derive the CWIN
from a Reno like algorithm, avoiding the need to estimate max RTT
during the initial startup.

Introduces a "probe level" with progressively increasing rates of
probing as previous trials succeed.

Added implementation considerations.

## Changes since draft-huitema-ccwg-c4-spec-00
{:numbered="false"}

Rewrote the description of the Initial state in {{c4-initial}}
to remove dependency on nominal max RTT.

Added the specification of reaction to ECN in {{process-ecn}}
and in {{rate-reduction}}. Update section {{c4-pushing}} to
modulate pushing rate based on observed rate of ECN/CE marks.

Added the RTT margin consideration in {{set_pace}}, and 
changed the computation of the "quantum" from:

~~~
quantum = max ( min (cwnd / 4, 64KB), 2*MTU)
~~~

to:

~~~
quantum = max ( min (pacing_rate*4_milliseconds, 64KB), 2*MTU)
~~~

The old formula caused long bursts of packets that would
trigger packet drops or ECN/CE marking by active queue management
algorithms.






