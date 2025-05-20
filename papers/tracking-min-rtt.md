# Tracking Min RTT

Delay base congestion control algorithms monitor the "round trip time" (RTT) of
packets, that is the time between a packet is sent and its first
acknowledgement is received. They make decisions by
comparing the RTT to a "base line" value, usually the "min RTT". 
They use the difference
between the mini RTT and the latest RTT to estimate the amount of
queuing happening in the network. Some algorithms, like TCP Vegas
(see {{https://dl.acm.org/doi/10.1145/190809.190317}})
base their control on the ratio between the actual RTT and the min RTT.
Other, like LEDBAT {{https://www.rfc-editor.org/rfc/rfc6817}}, monitor
the difference between the actual RTT and the min RTT. In both cases,
we find similar issues: using a wrong estimate of the base RTT leads to
wrong decisions by the congestion control algorithms.

The most basic algorithm defines the min RTT as the minimum value of the
RTT since the beginning of the connection. This is a simple algorithm,
but it has two main issues:

* Different connections sharing the same bottleneck do not always
  observe the same min RTT. Established connections
  may build a "standing queue" at the bottleneck, new connections will observe
  a higher min RTT, and this leads to the  "latecomer advantage"
  (as discussed in the LEDBAT RFC).
* Change in the network configurations may cause the RTT to change.
  If the RTT gets smaller, the new value should be rapidly observed.
  But if the new RTT is higher, the basic algorithm will not observe
  a new minimum and will mistake the increased RTT as congestion
  signal (see discussion of "rerouting" in
  {{https://people.eecs.berkeley.edu/~ananth/1999-2001/Richard/IssuesInTCPVegas.pdf}}).

There are many proposed solutions, which all have their own tradeoffs.

## Using natural traffic variations

Large bulk transfers may keep a high transmission rate for a long time,
but many other applications don't. For example, a typical web browsing
session might see a peak of traffic when the a new web page is loaded,
followed by much lighter traffic when the user is reading that page.
A Real-Time Communication (RTC) application will see peak of traffic
when sending "initial" video frames, followed by lighter traffic
when sending "differentially encoded" video frames.

If two connections share the same bottleneck and one of them enters
a "low traffic" phase, there should be shorter queues at the bottleneck,
and both applications should observe shorter RTT. The latecomer connection
could observe a shorter RTT than what it initially observed
and update its estimate of min RTT. That might be sufficient to
reduce the latecomer advantage.

This might be a practical solution in many cases, but it will not
work if the applications are all high bandwidth. It will not
by itself solve the "rerouting" issue, since the min RTT could
only decrease over the lifetime of the connection.

## Sliding Window of Min RTT

Instead of measuring the minimum value of the RTT since the beginning
of the connection, we could measure the minimum RTT over some kind
of sliding window, such as "the last X seconds" or "the last N RTTs".

This is generally considered a bad idea, since it can trigger a
vicious circle of base delay increases. At the end of each
sliding window, the new base delay reflects the sum of the
actual min RTT and the delays caused by standing queues at the
bottleneck. If connections increase their "min RTT" value,
the standing queues will increase some more, and that will
lead to further increase of the "min RTT" after the next
measurement window. That's not our design goal!


## Forcing traffic slowdowns

If the traffic does not naturally slow down, the congestion
control algorithm can force it to do so. This strategy is
proposed in LEDBAT++
(see {{https://datatracker.ietf.org/doc/html/draft-irtf-iccrg-ledbat-plus-plus-00}}).
LEDBAT++ enforces "slowdown periods" to improve
fairness between competing connections: "A slowdown is an interval
during which the LEDBAT++ connection voluntarily reduces its traffic,
allowing queues to drain and transmission delay measurements to converge
to the base delay." During these intervals, the congestion
window is drastically reduced to just 2 packets, before growing
back to "ssthresh" per the slow start algorithm. The slowdown intervals
are computed to not reduce the overall bandwidth by more than 10%.

The proposed strategy works for LEDBAT, which is designed to deliver
"lower than best effort" performance, but it is not optimal for all
applications. For example, enforcing a slowdown period for RTC application
could result in periodic "freezing the video", which reduces the
quality of the user experience.

Just like natural variations, forced slowdowns cannot solve the
"rerouting" issue.


## Periodic reset of Min RTT

We can try to address latecomer and rerouting issues by enforcing
occasional resets of the min RTT. In the simplest form, the reset
will just forget everything about the existing connection, reset
the congestion window to a small initial value, reset the min RTT
to the last observed value, and then follow the startup mechanism
to discover the new congestion window and the new min RTT.

Just like forced slowdowns, the reset will result in lower traffic
at the bottleneck, allowing competing connections to observe a
lower min RTT and thus mitigating the latecomer advantage issue.
The reset also solves the rerouting issue, because it discovers
a new value of the min RTT based on the new network conditions.

Like enforced slowdowns, the periodic reset results in poor user experience
for RTC applications.

## Combining Reset and Monitoring

We can try to mitigate the user experience issues of the "periodic reset"
strategy by monitoring the "natural slowdowns" of the connection.
The BBR algorithm (see
{{https://datatracker.ietf.org/doc/html/draft-cardwell-iccrg-bbr-congestion-control}})
uses a variant of that strategy. It detects natural slowdowns by monitoring
changes to the "min RTT" and remembering the last time the min RTT was
reduced to a lower value. If that value has not been updated in the last
5 seconds, the connection enters the "ProbeRTT" state. It limits the
transmission to half the measured rate, and remains in that state for
the longest of 1 RTT or 200ms.

Entering ProbeRTT does not directly affect the value of the min RTT.
Instead, BBR maintains a "min RTT filter", containing the minimum values
observed in the last 10 seconds. The value acquired after the ProbeRTT
roundtrip will typically constribute one data point to that filter. Since
the process is run every 5 seconds, the filter will typically contain
2 such values, the smallest of which will be used as the new minRTT.

The BBR strategy will still result in periodic reset and bad experience
if the application does not naturally produce natural slowdowns in
the last 5 seconds, or if these slowdowns did not result in a reduction
of the min RTT. This would of course happen after a rerouting event,
and it might also happen in some cases of competition at the bottleneck.

## Monitoring Natural Slowdown

The BBR strategy appears robust, but it can be improved by more fully
exploiting the "natural" slowdowns that are frequent in RTC applications.
BBR's specification is complex, but it boils down to requiring two
successive "probe RTT" trials before accepting to increase the
min RTT. We ought to get something almost equivalent by requiring
two natural slowdowns, or a mix of 2 forced and natural slowdowns. This
should result in faster detection of RTT increases, and thus faster
adaptation to the new networing conditions.

BBR relies on an "RTT min filter" that holds the min RTT observed over
the last 10 seconds. This is somewhat resource consuming and adds to
implementation complexity -- and one of the objectives in our design
is to limit that complexity. A simplification would be to just four
variables: the nominal min RTT, the time of the last update,
a running minimum since the time of
the last update, and the total number of slowdowns that were completed.
These counters will be updated as follow:

* if a new RTT is measured below the nominal min RTT, apply it
  and reset the time of the last update, the running minimum and the
  count of slowdowns.
* also reset these counters if new RTT is measured a a value close
  to the nominal RTT.
* if we observe a natural slowdown, increase the number of slowdowns
  by 1 one RTT after that slowdown.
* if 5 seconds have elapsed since the last update, force a slowdown.
* if 2 slowdowns have been observed, replace the nominal min RTT
  by the running minimum and reset the counters.

## Managing competition scenarios

BBR uses the same mechanisms to manage competiton and to manage RTT
increases. This increases the risk of entering the vicious circle
noted above in the discussion of Sliding Window of Min RTT. It seems
safer to keep the two mechanisms separate.

The implementation will have to take into account the mitigations of
competition and of chaotic delays. These mitigations force the
algorithm to not respond to RTT variations, or at least alter
its response. The running minimum may
continue to be updated, but the slowdown count should not be
incremented during these periods, and new slowdowns should not be forced.

## Setting the duration of the slowdown

Some implementations, including picoquic, apply a low pass filter
to RTT measurements and smooth short term jitter. These low pass
filters may mask the brief dip in the RTT measurement that we expect
to see during a slowdown. The robust solution is to ensure
that the slowdowns last long enough to allow the new RTT value
to become visible despite the filters. We believe that maintaining
the slow down state for 2 RTT is sufficient for that. The practical
consequences are:

1. If the code is forcing transition to a slowdown state, the
   algorithm should remain in that state for 2 RTT.
2. If the code is monitoring "natural" slowdowns, it should
   only consider natural slowdowns that last at least two RTT. 

The "2 RTT" requirement also provides more confidence that
peers sharing the bottleneck will observe a dip in traffic and
adjust their own evaluation of the min RTT.


## Adapting the BBR design to CWIN based control

C4 manages a single control variable, the congestion window. That's different
from BBR, which manages pacing rate and congestion window independently.
When BBR enters the "probe RTT" state, it halves the pacing rate, but
use a large congestion window. The endpoint will continue sending
at the specified rate even if the rtt_min is under estimated. In contrast,
if C4 halves the congestion window, it will stop sending after about
1/2 the minimum RTT, even if the RTT was significantly higher.

Stopping sending causes an interaction with the "suspension" mechanism,
which reacts to the lack of arrival of new acknowledgements. If transmission
stalls, the suspension mechanism will fire erroneously. At a minimum, we
should stop processing the suspension signals when in "slowdown" mode.

## Dependency on min RTT

BBR is designed to spend most of the time in the CRUISING state,
with the pacing rate set to the measured value and the CWIN set to
twice the product of pacing rate and min RTT. This makes BBR
somewhat insensitive to a errors in min RTT estimate. As long
as the estimate is not off by 100%, the transmission will
continue.

The CWIN design is much more sensitive to errors in the
minimum RTT. It cannot send more than just one transmission
window worth of packets per actual RTT. If the RTT is off
by 10%, we get a 10% slowdown. If it is wrong by 100%, we
get a 50% slowdown. It is thus much more urgent to correct
wrong RTT estimates with C4 than with BBR.

Endpoints can notice that the RTT is probably too high when
RTT measurements are repeatedly larger than the min RTT. If we
merely mimic the BBR design, this will result in a first
slowdown after 5 seconds, a confirmation slowdown after
10 seconds, and only then an upward adjustment of the min RTT.
This seems way too long, but we have to remember that RTT
increases can be caused by either rerouting or congestion.
The min RTT should be adapted after a rerouting event, but
responding to a congestion event by upping the min RTT
can lead to a vicious circle in which all competing actors
increase their min RTT and their congestion window, making
the congestion worse.

The plausible solution is to perform a slowdown shortly
after noticing the delay increase is noticed. If the delay
increase was due to congestion, the slowdown will cause
queues to drain a bit, and the measured RTT should get
closer to the minimum. If it is due to rerouting, the
measured RTT will reflect the actual value. Performing
the slowdown twice will confirm that value.

This can be implemented by changing the "5 seconds" constant
to a variable, making it proportionally shorter if the
average RTT is higher than the min RTT.

We may need a safeguard to limit the overhead if the routing
is unstable and the rerouting happens too often. For example,
we can impose a mandatory 5 second delay between the last
two slowdowns and the the next one.
