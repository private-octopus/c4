# Growth and Competition

Congestion control algorithms have to manage growth. In the startup phase, they
need to quickly assess the available bandwidth, and tune the control
parameter accordingly. If the bandwidth is limited, the control
parameter should be adjusted to avoid creating queues. If the network
conditions improve, the parameters should be adjusted to take
advantage of available resource.

Congestion control algorithms also have to manage competition. If multiple
connections are sharing the same bottleneck, they must share -- not
necessarily perfectly
equal sharing, but certainly not a scenario in which one connection
hogs the resource and another starves.

## Initial Design

C4's start connections much like many other algorithm. Once the
startup is complete, our initial design for C4 adopts was somewhat similar to BBR:

* Cruise for several RTTs, with the congestion window set
  to the "nominal" value,

* Push at a higher rate for one RTT, typically using a CWND 25% higher
  than the nominal value.

* Recover, until the packets sent during
  the Push period have been either acknowledged or declared lost.

When ACKs are received, consider the number of bytes acknowledged
between the time the packet was sent and the time it was acknowledged.
If the RTT at the time of the ACK arrival is larger than min RTT,
reduce that number proportionally. Set the nominal CWND to
the maximum of the corrected value and the old nominal value.

If congestion signals are received outside of the recovery period,
immediately reduce the nominal CWND by a coefficient beta, typically
set to 25% -- but modulate the coefficient so that slight increases
in RTT or in ECN/CE marking rate only cause a slight reduction of
the nominal CWND.

## Assessment of Initial Design

Increasing the window by 25% during pushing periods
allows discovery of available bandwidth.
It also introduce enough
disorder to ensure that no connection monopolizes the network resource.
But it has a few downsides:

* When the CWND is increased by 25%, real-time application can schedule
  more "low priority" packets. This is beneficial if the assessed CWND was
  too small, but if it was "just right" this leads to priority inversion.
  The extra packets are queued in the network and may delay transmission
  of consecutive higher priority packets.

* If the network capacity suddenly improves, C4 will catch up faster
  than Reno or Cubic, but will still have to wait several cycles of cruise-push-recover
  before the CWND reflects the available capacity.

* The connection may be computing for bottleneck capacity with other
  non-C4 connection, and in particular with connections running Cubic.

There are plausible solutions to all these issues. If the connection
is running at or near full capacity, it may decide to push less often,
or to push at a lower rate than 25%. If the connection senses that
there might be more capacity available, it may decide to push harder,
or more frequently. It might even decide to re-enter the "startup"
mode, to quickly assess the available bandwidth. If competing with Cubic,
the connection may switch to a "pig war" mode in which it stops reacting
to delay increases.

The problem of course is that these solutions have side effects. Pushing
slower or less often mean being less aggressive, pushing too hard risk
creating queues, not reacting to delays risk increasing delays.

## Decisions and Monitoring

We are looking at the following decisions:

* exit startup if the CWND does not increase significantly for 3 consecutive RTT,

* use a lower push rate if the last push did not cause a
  significant increase of the nominal CWND,

* switch back to higher push rate if the nominal CWND did increase significantly
  for the last push attempt,

* re-enter startup if the nominal CWND did increase significantly
  for three push cycles,

* start the pig-war mode if the nominal CWND decreased 3 times due to
  delay feedback without increasing significantly,

* exit pig-mode if the CWND did increase significantly
  for three push cycles.

The nominal CWND can only increase if the path CWND was set higher,
which only happens in the "initial" and "push" state. The higher
window may cause large ACKs to arrive in the next RTT.

We test whether the CWND increased enough by comparing the nominal
value at the end of the era to the value it had before the previous
push or initial era. The comparison is done at the end of the
recovery or initial era. "Increased enough" depend on the
"alpha" coefficient at the beginning of the previous era:

- initial use alpha = 200%; enough is defined as "at least a 25% increase",
  because this is the value used for BBR.

- if push used alpha = 125%, enough is defined as 12.5%.

- if push used a lower value, alpha is defined as 0%.

We need to also monitor the congestion signals received during the recovery
(or second initial) phase. If congestion signals were received, we have an indication
that the connection was pushing too hard, which should invalidate the "increased"
test.

The "not increased" test is true if the "increased" test is false.

The "decreased" test is true if the nominal
value at the end of the era is lower than the value it had before the previous
push or initial era.

The "decreased and delay limited" test is true if the bandwidth decreased, or
remained the same and delay based congestion signals were present. The count
of periods for which the delay did not increase should be reset if the
bandwidth did increase significantly. (Low value of alpha mean a more
complex evaluation).

The signals are evaluated at the end of the recovery or initial period, and
the transitions based on such signals are performed at that point.






## Making Push more Dynamic

We tried three improvements to try mitigate these two perceived issues: push
less if the connection is nearly congested; push sooner if the previous
push was successful; and, switch to startup if the last 3 pushes all resulted
in capacity increases.

## Lighter Push if Stable

The first modification is to push lighter if the CWND is correctly evaluated.
For example, if we tried a 25% push and it did not result in a significant
increase of CWND

If the CWND is correctly estimated,



