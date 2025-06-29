# Probing vs. Latency

## Congestion Control Needs Probing

Capacity decrease is easy to detect
* Signals like packet loss, excessive RTT, ECN/CE marks
* Capacity increase not so much

Absence of congestion signals means capacity MAY have increased, but it does not say by how much!
Classic Solution is to probe:

* Reno, Cubic: continuous CWND increase until triggering a packet loss
* Vegas, Ledbat: same, but until RTT increases above minimum
* BBR: periodic “probe bandwidth up”, send 25% above rate for 1 RTT, measure if it worked

C4 follows the BBR strategy, periodically moving to a "push" state in which either the rate or the CWND increases by 25%.

## Downsides of Probing

The first obvious downside of probing is bufferbloat.
If every connection constantly tries to increase its sending rate and
only stops when it experiences packet loss, it will tend to fill the bottleneck buffers,
thus creating huge delays. This is a well recognized issue, addressed by switching to new
congestion control algorithms that react to other signals, such as ECN/CE marks, or increased
delays. BBR, Prague, vegas, LEDBAT or indeed C4 all do that.

The next problem is inertia. The connection will only receive a congestion
signal a full RTT after it starts sending at an excessive rate. It will
react, but at that point there is a full RTT of packets in flight at the
excessive rate. These packets will fill the queues, cause increase delays,
and may trigger packet losses. 

Packet losses translate into increased
latency for interactive applications, because application data can only
be processed after recovering from all losses. Even large batch transfers
will suffer if the packet losses happen during the last phase of the
transfer.

For real time connections, the consequences of too much probing are even worse.
Let's assume that these application rely on a packet scheduler informed
by the state of the congestion control algorithm. They will send the
real time packets first, and then send only as much of the lesser
priority packets as the congestion control permits.

This works well in general, but fails if the congestion control algorithm
overestimates the network capacity. In that case, the application will
send more low priority traffic than the network can process. These
packets will "clog the queues" and delay the real time traffic,
degrading the quality of experience. That's exactly what happens
when the congestion control algorithm enters a "probing" phase.

## Balancing Benefits and Risks of Probing

We certainly need some probing. We must assume that in a long session
the network capacity will change multiple times, sometimes decreasing,
sometimes increasing. Congestion control will detect the decreases and
lower the sending rate. If it never detects the increases, the
sending rate can only decrease over time, leading to underestimation.
Settling for an underestimated sending rate degrades QoE. 
Video will be compressed more than necessary
Low priority traffic will experience large delays.
But if we know that we require probing in general, we also know that excessive 
probing may degrade latency and degrade QoE. We need a tradeoff.

Currently, C4 engages moves to a "push" state after a number of RTT, typically
6 to 8 if the bandwidth is not large enough to just send high definition video.
During the "push" state, we increase the sending rate by 25%. After 1 RTT, C4
quits the push state and remains in a "recover" state until all packets sent
during "push" are acknowledged. Monitoring the acknowledgements received during
the "recover" state will reveal whether the observed bandwidth increased or
not.

If the observed bandwidth did increase, we know that the previous sending data
rate was under estimated. It is logical to keep the probing rate just as high as
before. On the other hand, if the observed bandwidth did not increase, it is
reasonable to assume that the previous sending rate was correct, and that we
don't need to push so hard. We may remember that, and ensure that next time,
we only need a low-touch test, maybe 5%, thus reducing the impact.

And then we can go on like that: if the previous probe was successful, whatever its
rate, probe at 25%. If it was not, probe at 5%.

## What about App-Limited

Connections often go through "app limited" stages, during which the application
sends little data. For example, in an interactive application, the server will
wait for new client requests before sending more data. In a video conferencing
application, we will see peaks of bandwidth when the codec produces I-frames,
followed by extended periods during which the codec produces differentially
encoded P-frames that require much less bandwidth. Probing when the application
sends no-data or little data is pointless: the application will not send
more data than the estimated data rate, let alone 25% more.

A reasonable strategy would be to only start a probing phase when the connection
is not "app-limited". At that point, the tension between risks and benefits of
probing still exists: loosening the congestion control too much will
cause sending too many low priority frames, but not probing will cause settling
for insufficient sending rates. Here again, we should alternate between
probing at 25% and 5%, as explained in the previous consideration.

## What about startup

The considerations above apply during startup just as much as during probing phases.
During startup, the congestion control allows application to send at twice the
estimated rate, and sometimes more. This allows exponential growth of the
estimate, until it matches the capacity, but it also allows the creation of
large queues. Seems very similar to the problem we analyze here, but with different
requirements and diff probably leading to different tradeoffs, better analyzed
separately.

On the other hand, experimenents point to a specific recommendation. Algorithms like BBR
would remain in startup mode until 3 successive RTT show no significant growth in bandwidth,
but make an exception for "app limited" RTT intervals. This means that when the next
non-limited interval happens, the congestion control is very loose -- the limit is set
to more than twice the existing data rate. This has all the downsides the we saw previously,
e.g., queuing a large amount of low priority data and jeopardizing real-time performance.
It seems much safer to exit slow start early, and then use probing intervals to detect
whether the algorithm is operating below capacity.







