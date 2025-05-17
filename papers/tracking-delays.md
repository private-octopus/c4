# Tracking Delays, or not

C4 is designed to deliver low latency, and does that by tracking the observed
round-trip-times (RTT) of packets. This generally works, except in at least
two scenarios: when competing with loss-based congestion control algorithms
such as Cubic (see {{./vegas-like-compete.md}}), or when the delays measurements
are affected by high jitter.

## Tracking delays

C4 monitors the RTT of successive packets. It is considered excessive if
it exceeds the "min RTT" by a significant marging, set to the minimum of
1/8th of the min RTT and 25 milliseconds. If 7 successive packets
exceed that minimum, C4 declares a "delay based" congestion event.

## Limiting window increases

C4 does not only react to excessive delays, it tries to avoid them. When
C4 processes a packet acknowledgement, it examines the number of bytes
acknowedged since the packet was sent, which is closely related
to the number of bytes in flight after the packet was just sent. If
that number exceeds the current "nominal CWIN", the CWIN could be
increased.

The goal is to increase that CWIN without too much risk of creating queues
in the network. C4 evaluates that risk by comparing the observed RTT
to the min RTT. If the observed RTT does not exceed the min RTT by
more than a small margin, the new value is accepted. Otherwise,
C4 computes:
~~~
corrected_bytes = acked_bytes * (RTT+margin)/(observed RTT)
~~~
If the corrected bytes value is larger than the nominal CWIN, the nominal
CWIN is set to that value.

## Competition with Cubic

If a delay based CCA competes with a loss based CCA like Cubic, Cubic wins.
Cubic will try to fill the path buffers, resulting in long static queues.
the delay based algorithm will detect these queues and react by lowering its
sending rate, until it only uses a tiny fraction of the path's bandwidth.

C4 detects competition by monitoring successive delay-based decisions. If
C4 reduces rates and enters recovery due to excessive delays
three times in a row, it sets a
"competing" flag. When that flag is set, it C4 stops reacting to delays. It
will only reduce bandwidth if it detects packet losses or ECN marks.

C4 detects that competition has ceased by tracking the number of successive
"push" trials that did not result in a congestion notification (packet loss or
ECN/CE marks). This is a signal that the connection can increase its
transmission rate, from which we can reasonably infer that competition
has ceased. (The same signal, in the absence of competition, indicates
that network conditions have significantly improved.)

## Chaotic jitter

We observe that under some conditions that round trip times of packets
delivered through Wi-Fi networks vary widely. We can see networks in which
the minimal RTT is a few milliseconds, the average RTT is maybe 5 to 10
milliseconds, and we can observed outliers RTT samples at 100ms, 200ms,
or more. This has three consequences:

* the loss detection algorithms will sometimes declare a packet as
  lost when in fact it is merely delayed.
* the delay tracking will sometimes declare congestion events when
  the network is not actually congested
* the number of acknowledged bytes will be "over corrected" because
  of the excessive delay

C4 detects the "chaotic jitter" condition when the maximum delay recently
observed is much higher than the minimum delay. C4 maintains an estimate
of the maximum delay my computing a running average, updated at the
end of each era:
~~~
nominal_max_rtt = (7 * nominal_max_rtt + era_max_rtt) / 8
~~~
Where "era_max_rtt" is the maximum RTT observed in the last era.

The condition is set using a low/high mark mechanism:
~~~
if nominal_max_rtt > 7 * min_rtt:
    chaotic_jitter = True
elif nominal_max_rtt < 2 * min_rtt:
    chaotic_jitter = False
~~~
When the "chaotic jitter" condition is set, C4 will not signal
congestion based on delay increases. When processing an acknowledgement,
the computation of corrected bytes is changed to:

1. Set a "target RTT" as:
~~~
target_rtt = (3*min_rtt + nominal_max_rtt)/4
~~~
2. If the observed RTT is larger than the target RTT, correct the
   number of acknowledged bytes as:
~~~
corrected_bytes = acked_bytes * (RTT+margin)/(observed RTT)
~~~

## Comparison

Test of a 4MB download over a "chaotic" wifi connection:

* C4: download in 3.34sec, with the RTT pegged at about 30ms
* BBR: download in 3.35 sec, with the RTT varying between 30 to 45 ms.
* Cubic: download in 3.34sec, the RTT is pegged at 80ms.

##TODO:
The part about chaotic jitter is questionable. If we don't manage that flag at all, we don't seem to have worst performance, or at least not much worse.
Repeat the tests with media simulation.