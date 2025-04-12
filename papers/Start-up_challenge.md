The initial startup phase is critical for both TCP and QUIC. The goal is to quickly discover the network
capacity, so transmission can start at full speed, without causing too much queues and packet losses.

State of the art:

* slow start, Van Jacobson, 1988. RFC 5681. Increase CWIN by 1 for each packet ACKed, i.e., double every RTT.
  Stop on first packet loss -- i.e., after filling the network buffers.
  Simple, discovers the network capacity in logarithmic time.
  Downside: filling the buffers creates huge queues and make the network bad for everybody.
  Also, at the time of the first loss, a full CWIN of packets is in flight.
  Since the buffers are full, most will be lost, and will have to be resent later.

* Hystart, Sangtae Ha, Injong Rhee, 2011. Monitor RTT and uses increase in RTT as a signal to exit slow
  start before potential packet loss occurs. Still creates some big queues, because there is a full
  CWIN of packets is in flight at the time of detection, typically one bandwidth delay product,
  but the queues will at most double the RTT. Will avoid losses if buffers are large enough and
  if the detection happens soon enough, but not always. Specific issue is that RTT is a noisy
  signal, due to delay jitter, so Hystart will sometimes exit too soon.

* Hystart++, Praveen Balasubramanian et al, 2023, RFC 9406. Try to address the delay-jitters issue
  of Hystart by adding a "retry" period at a lower increase rate after Hystart exits on delay increase.
  Does not solve the queuing and potential losses issue.

* BBR Start up, BBR team, probably around 2018. Use rate control instead of delay control.
  Monitor the bandwidth from ACKs, set the pacing rate to twice the bandwidth, stops if the measured
  bandwidth does not increase for 3 RTT. In theory, avoids losses. Also, observing absence of growth
  for 3 RTT protects against a nework event slowing the transmission for 1 RTT and leading to
  a false exit -- logically, this would not happen 3 times in a row. However, will spend the last 3 RTT
  sending at twice the nominal bandwidth, which will create large queues and delay increase
  (double the RTT) for 3 RTT -- or packet losses if the network does not have enough buffers to absorb the queues.

* Packet pairs (Maybe Jean Bolot, 1993 -- but I have also heard it attrbuted to Hari Balakhrisnan).
  Send two packets back to back. measure the interval when they are received. It measures the time it took
  the network to forward one packet. You can then estimate the network speed, and use that to tune the slow
  start algorithm. Neat idea, but terribly imprecise -- the delay between two packets can be affected
  by jitter for all kinds of reasons.

* Paced Chirping (Bob Briscoe, 2019): instead of just sending packet pairs, send a "train" of packets
  at a high rate. Make the train small enough to not create a big queue, but large enough so that measuring
  the arrival delay of the train can give a good idea of the network bandwidth. Cool idea, mostly works,
  but measures more the "instant capacity" than the actual share that a user can safely use.

* Careful Resume (draft in progress): remember the data rate and RTT of a previous connection, reuse
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