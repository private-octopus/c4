# Christian's Congestion Control Code - C4

The C4 project look at options for Internet Congestion Control. When running Real-Time applications,
we want to use a congestion control code that lets applications use the full capacity of the network,
avoids building queues in the network, and promptly detects changes in network conditions so
applications can adapt their behavior.

The first stage of the project is to prepare a series of papers discussing congestion
control design issues such as:

* how to quickly drive connection from a cold start to sending at network speed? Is
  Hystart ()? What about [Hystart++](https://www.ietf.org/rfc/rfc9406.html)? Maybe a
  helping of [Careful Resume]( https://datatracker.ietf.org/doc/draft-ietf-tsvwg-careful-resume/)?
  See the [start up discussion paper](./papers/Start-up_challenge.md).

* how to combine multiple signals like delays, ECN marks and packet losses to detect
  congestion? Should there be a differential behavior, such as slowing down faster if
  packet losses are detected than if delay increases? Does that affect the "recovery"
  mechanism introduced in TCP Reno? We propose a specific "most restrictive signal"
  rule in the paper on
  [Combining congestion signals](./papers/combine_congestion_signals.md).

* algorithms that try to minimize delay tend to be too polite. That's the main reason why
  [TCP Vegas](https://sites.cs.ucsb.edu/~almeroth/classes/F05.276/papers/vegas.pdf)
  can only be deployed in controlled environments. TCP Vegas notices delay increases
  before losses happen, backs off first, and eventually yields most of the network capacity to
  the competing connection using loss based algorithms like Reno or Cubic.
  A deployable algorithm will have to
  deal with that issue, maybe by adopting a polite delay minimizing attitude by default, but
  switching to a more aggressive posture when competing with Reno or Cubic. This is easier said
  than done. See the discussion paper on [vegas like competion issues](./papers/vegas_like_compete.md)

* fair sharing between connections requires that connections using
  lots of bandwidth back off faster than slower connections. But algorithms have to scale
  over order of magnitudes of bandwidth, from a few 100 kbps to 10 Gbps. Scaling requires that
  both increase and backoff be proportional to the current throughput, in which case
  we lose the fairness provided by the combination of linear increase and
  multiplicative decrease. What gives?

* If we accept to not guarantee fairness, we will need to at least guarantee that
  other connections do not "starve". How do we do that?

* In competitive scenarios, connections can only increase their share of bandwidth
  by forcing some other connections to yield. How do they do that? Are slow increases
  sufficient, or do we need "push" events with sizeable increase, maybe at least 25%?

* Real time connections are often application limited. For example, video transmission
  will alternate
  between periodic refresh that may require lots of bandwidth, and differential
  updates that require very little. Can that interact well with bandwidth seeking
  congestion control algorithms?

* delay based algorithms have to compare measurements with a reference "min RTT". A classic
  implementation is to consider the min RTT since the beginning of the connection, but
  that leads directly to the "late comer advantage" problem. Adjusting the reference
  RTT to current conditions leads to an opposite issue, in which the reference RTT slowly
  drifts up over time. BBR solves that with periodic resets, but these periodic resets
  degrade the performance of Real-Time applications. Is there a better way?

* what is the best way to take advantage of network support for low latency, such as
  the [dual queue mechanism of L4S](https://www.rfc-editor.org/rfc/rfc9332.html) or
  the isolation features of other Active Queue Management algorithms like
  [Flow Queue Codel](https://datatracker.ietf.org/doc/html/rfc8290),
  [PIE](https://datatracker.ietf.org/doc/rfc8033/) or
  [FQ-PIE](https://www.ietf.org/archive/id/draft-tahiliani-tsvwg-fq-pie-00.html)?

* how do we handle special conditions like the "suspension" of Wi-Fi networks, or
  the simultaneous variation of bandwidth and delays in "low Earth orbit" satellite
  networks?

We can test these scenarios using the simulation tools in the Picoquic test suite,
through the `pico_sim` tool. The tool will execute a simulation scenario, and
compute qlog traces for the connections in that scenario. The simulation
scenario is specified in a text file. Some examples of simulation scenarios are provided
in the `sim_specs` folder.

The qlog traces produced by the simulation scenarios can be visualized with the
Python script `qlogparse.py`, which produces a graphs showing how key parameters
like congestion window or RTT vary for the simulated connections.

Our first goal is to collect a robust set of simulation scenarios to investigate
our list of issues, and refine our series of documents. This may require some
updates to the simulation tool, for example to simulate various AQM policies,
or to incorporate the simulations of Wi-Fi links. 

Once we have these documents and simulations, we can start developping
"Christian's Congestion Control Code".