# Combining congestion signals

Congestion control algorithms can discover that the network is becoming congested by
monitoring different signals:

* Classic TCP only reacts to packet losses, per [RFC 5681](https://www.rfc-editor.org/rfc/rfc5681).
  This is simple to implement, but makes the assumption that most packet losses are the result
  of queues overflowing due to excess traffic, which is not necessarily true when the
  network uses wireless transmission. Also, reacting only in case of packet losses
  tends to fill up network buffers and increase the round trip times (RTT),
  see [Bufferbloat: Dark Buffers in the Internet](https://dl.acm.org/doi/pdf/10.1145/2063176.2063196),
  by Jim Gettys and Kathleen Nichols, 2011.

* Protocols like [TCP Vegas](https://sites.cs.ucsb.edu/~almeroth/classes/F05.276/papers/vegas.pdf)
  and LEDBAT ([RFC6817](https://www.rfc-editor.org/rfc/rfc6817)) have shown that we
  can maintain low end to end latency if the congestion control protocol monitors
  the RTT and reduce the transmission rate if delay increases. There is a known issue
  that such protocols do not compete well with loss based congestion
  controls algorithms like classic TCP or Cubic ([RFC9438](https://www.rfc-editor.org/rfc/rfc9438)).
  See the discussion paper on [vegas like competion issues](./papers/vegas_like_compete.md).

* The first version of BBR 
  [BBR Draft version 00](https://datatracker.ietf.org/doc/html/draft-cardwell-iccrg-bbr-congestion-control-00)
  did not monitor either packet losses or RTT, but simply measured the effective throughput
  of the connection, and used pacing at that rate to  minimize network queue. This provides
  good results, but proves somewhat unfair when competing with connections that react to
  packet losses. A small downside is that bandwidth measurements typically happen two RTT after
  a decision to modify transmission rates, which can be a bit slow.

* TCP Prague, developed as part of the L4S effort ([RFC9330](https://datatracker.ietf.org/doc/rfc9330/))
  monitors the arrival of ECN marks, slows down if the frequency of ECN marks increases
  and modulates increases if the frequency of ECN marks is low. This is shown to
  result in low delays and good network utilisation, but it assumes that network
  routers are modified to monitor network utilization and issue ECN marks when the
  network is becoming overloaded.

All these signals have advantages and downsides. A modern algorithm should probably
combine all these signals: react to ECN marks when they are available, to packet losses
when they happen, and monitor either the round trip times or the effective bottleneck
bandwidth to avoid building queues even in the absence of ECN marks. This approach is used in
the [current version of BBR](https://datatracker.ietf.org/doc/html/draft-cardwell-ccwg-bbr).

The basic of combining multiple signals is simple: if a signal arrives that suggest
congestion, whether a delay increase, an ECN mark or a packet loss, react immediately.
The signal is driven by decisions made 1 to 2 RTT ago. Some of the packets sent with
the old decision will still be in transit. If we change transmission parameters
now, the first packet sent with the new parameters will be acknowledeged in 1 RTT.
Classic TCP handles that by introducing a "recovery" process:

* upon a packet loss, reduce the congestion window and enter recovery,

* do not react to further packet losses until the first packet sent after
  the transition to recovery is acknowledged (or lost)

* resume normal processing after that first packet after that first packet
  is acknowledged.

We can reuse the same logic if we treat all congestion control signals
equally, but we may want to use some differentiation. For example:

* if the rate of ECN marks exceeds the set threshold, reduce the CWIN
  proportionally to the difference between the observed rate and the threshold.

* if the rate of packet losses exceeds a set threshold, reduce the CWIN
  by 30% (i.e., the coefficient "beta" of BBR)

* if the RTT exceeds RTT_MIN my more than a set margin, reduce the CWIN
  proportionally to the excess over that margin.

If we ignore congestion signals while in recovery, we will only react to a first
signal, while a second one might have cause a larger reduction. In C4, we plan
to use a combination rule:

* when a congestion signal happens while in recovery, compute by how much
  it would have reduce CWIN.
* if that newly computed CWIN is lower than the current, consider
  something like "re-entering recovery" and pick the lower CWIN value.


   still monitors the bottleneck bandwidth, but comine that with reaction to packet losses
   and ECN marks.
how to combine multiple signals like delays, ECN marks and packet losses to detect
  congestion? Should there be a differential behavior, such as slowing down faster if
  packet losses are detected than if delay increases? Does that affect the "recovery"
  mechanism introduced in TCP Reno?