The initial startup phase is critical for both TCP and QUIC. The goal is to quickly discover the network
capacity, so transmission can start at full speed, without causing too much queues and packet losses.

State of the art:

* slow start, Van Jacobson, 1988. [RFC 5681](https://www.rfc-editor.org/rfc/rfc5681).
  Increase CWIN by 1 for each packet ACKed, i.e., double every RTT.
  Stop on first packet loss -- i.e., after filling the network buffers.
  Simple, discovers the network capacity in logarithmic time.
  Downside: filling the buffers creates huge queues and make the network bad for everybody.
  Also, at the time of the first loss, a full CWIN of packets is in flight.
  Since the buffers are full, most will be lost, and will have to be resent later.

* Hystart, Sangtae Ha, Injong Rhee, 2011,
  [Taming the elephants: New TCP slow start](https://www.sciencedirect.com/science/article/abs/pii/S1389128611000363).
  Monitor RTT and uses increase in RTT as a signal to exit slow
  start before potential packet loss occurs. Still creates some big queues, because there is a full
  CWIN of packets is in flight at the time of detection, typically one bandwidth delay product,
  but the queues will at most double the RTT. Will avoid losses if buffers are large enough and
  if the detection happens soon enough, but not always. Specific issue is that RTT is a noisy
  signal, due to delay jitter, so Hystart will sometimes exit too soon.

* Filtering RTT for Hystart, Christian Huitema, 2017,
  [Implementing Cubic congestion control in Quic](https://privateoctopus.com/2019/11/11/implementing-cubic-congestion-control-in-quic/).
  Demonstrates that Hystart can exit too early on link susceptible to delay jitter.
  Proposes to remedy that by filtering the RTT measurements.

* Hystart++, Praveen Balasubramanian et al, 2023,
  [RFC 9406](https://www.rfc-editor.org/rfc/rfc9406). Try to address the delay-jitters issue
  of Hystart by adding a "retry" period at a lower increase rate after Hystart exits on delay increase.
  Does not solve the queuing and potential losses issue.

* BBR Start up, Neal Cardwell et al., 2017,
  [BBR Draft version 00](https://datatracker.ietf.org/doc/html/draft-cardwell-iccrg-bbr-congestion-control-00).
  Use rate control instead of delay control.
  Monitor the bandwidth from ACKs, set the pacing rate to twice the bandwidth, stops if the measured
  bandwidth does not increase for 3 RTT. In theory, avoids losses. Also, observing absence of growth
  for 3 RTT protects against a nework event slowing the transmission for 1 RTT and leading to
  a false exit -- logically, this would not happen 3 times in a row. However, will spend the last 3 RTT
  sending at twice the nominal bandwidth, which will create large queues and delay increase
  (double the RTT) for 3 RTT -- or packet losses if the network does not have enough buffers to absorb the queues.

* Packet pairs, Srinivasan Keshav, 1991,
  [A Control-Theoretic Approach to Flow Control](https://dl.acm.org/doi/pdf/10.1145/205447.205463)).
  Send two packets back to back. measure the interval when they are received. It measures the time it took
  the network to forward one packet. You can then estimate the network speed, and use that to tune the slow
  start algorithm. Neat idea, but terribly imprecise -- the delay between two packets can be affected
  by jitter for all kinds of reasons.

* Paced Chirping, Bob Briscoe, 2019, 
  [Paced Chirping: Rapid flow start with very low queuing delay](https://ieeexplore.ieee.org/abstract/document/8845072) ).
  Instead of just sending packet pairs, send a "train" of packets
  at a high rate. Make the train small enough to not create a big queue, but large enough so that measuring
  the arrival delay of the train can give a good idea of the network bandwidth. Cool idea, mostly works,
  but measures more the "instant capacity" than the actual share that a user can safely use.

* Careful Resume, G. Fairhurst et al.,
  [Convergence of Congestion Control from Retained State](https://datatracker.ietf.org/doc/draft-ietf-tsvwg-careful-resume/).
  Remember the data rate and RTT of a previous connection, reuse
  it with caution to speed up slow start.

The challenge is to combine all that. Maybe, start with slow start, but tweak the scheduling to create "trains"
of packets as in chirping. Deduce from chirping a high bound estimate of the data rate. Increase CWIN rapidly
to try match that data rate. Monitor the bandwidth (as in BBR) and exit if it does not grow for 3 RTT.
Also exit if the bandwidth matches the peak estimate. Switch to something like HyStart++ after that.

![graph showing C4 startup on a satellite connection](./picoquic-startup-satellite.png "Picoquic Startup Satellite")

The graph above shows various metrics during the beginning of a simulated geo-satellite connection
using Picoquic and C4. The pacing algorithm is tuned to operate at a high rate, and with a large
"quantum". This creates a series of intervals during which packets are sent faster than the ratio
of CWIN/RTT. Measuring the data rate during these periods provides a "peak bandwidth" estimate.
It could be plugged back in the startup algorithm, but precisely how to do it still requires
a bit of research.

## Delay measurement issues

As mentioned above, Hystart can exit too early on link susceptible to delay jitter. This issue is
solved in picoquic by filtering the RTT measurements to get rid of short term jitter.
However, filtering is a tradeoff. To get a reasonable amount of filtering, we need to consider
a sufficiently long series of measurements, 7 consecutive measurements in the case of Picoquic.
Acquiring these measurements takes times. When the path latency is short, the sender may
receive as few as 4 to 8 acknowlements per RTT. That means 1 or 2 RTT before the "delay exceeded"
condition is detected, by which time the transmission rate may have doubled or
quadrupled. The sender will exit startup, but there will be too many packets in flight.
The delay will be excessive, and there may be a large number of packet losses.

## Current solution

The current solution is to monitor the amount of data delivered between the moment the
acknowledged packet was sent and when it was received. The interval between such events
is the sum of the RTT, queuing delays and jitter in the ACk latency. We assume that
the jitter component is small compared to the full RTT and the queuing delays, and
that in any case the jitter will average out in successive measurements.

The amount of data delivered can be roughly estimated as:
~~~
delivered_from_sent_to__ACK ~= data_rate * (RTT+queuing delay)
~~~
From that, we can estimate a target CWIN by removing the queueing delay component.
Of course, we do not actually know the queuing delay, but we know that the RTT
without it would be about the same as the min RTT. This gets:
~~~
target_CWIN = data_rate * (RTT+queuing delay) * ((min_RTT + margin)/(RTT+queuing delay))
target_CWIN = delivered_from_sent_to__ACK * ((min_RTT + margin)/(RTT_measurement)
~~~
The sender maintains a "nominal CWIN", set to an initial value of 10 packets when
transmission starts. Over time, we will evolve this value:

* Upon each acknowledgement, set the nominal CWIN to the maximum of the current
  value and the `target_CWIN`,
* Reduce the nominal CWIN if congestion is detected.

The path CWIN is set to the product of CWIN by an increase coefficient:
~~~
path.cwin = nominal_cwin * beta
~~~
The value of `beta` varies with the state of the C4 congestion controller.
During the initial state, we set beta to 2, resulting in about the same
growth rate as classic TCP, Cubic, or Reno. At the end of the slow start
process, the path.cwin will be larger than the bandwidth delay product
of the path and the RTT measurements will increase and reach about twice
the min RTT. The nominal CWIN will stop increasing.

The sender will exit start-up if the nominal CWI does not increase for
3 consecutive RTT, or if congestion is detected through increasing rate
of packet loss or ECN marks, or increasing RTT.






