# Revisiting C4's Initial phase

Our November 2025 design of C4 included a "rate based"
initial phase, during which C4 will send at twice the "nominal rate",
monitor acknowledgments and increase the nominal rate if measurements
increase, and exit if congestion is detected or if the measurements
do not increase for 3 consecutive RTT. That algorithm works
well in most scenario, but we were observing early exits in
"high delay jitter" scenarios, such as Wi-Fi networks with lots of
packet collisions.

After observing that phenomenon, we realized that the
rate based algorithm was failing in case of high delay jitter
because it was setting the CWND to the product of pacing rate
and the "nominal" max RTT. The nominal Max RTT was set to a fixed
value, observed either before the initial phase or on the first
roundtrip in that phase. It would work if the initial phase
started during a high jitter event and the initial RTT was large
enough, but in many case it was not and became a limiting
factor.

## Why not increasing Max RTT during Initial phase?


In the initial phase, the algorithm tries to discover the bandwidth
and does not yet have a good estimate of delay jitter, which typically
requires a series of measurements. In these conditions, it is
easy to underestimate the max RTT. On the other hand, the flow is
deliberately probing at a high data rate. If the algorithm
allows updates of max RTT during that phase, the risks of
spiraling into buffer boat are very high, but if the CWND
remains too low, the risk of exiting startup with a severely
underestimated data rate is also very high.

We tried to develop simple rules to classify the delay measurements
between caused by jitter, and caused by congestion. If we could do that,
we would be able to increase the max RTT safely, when appropriate.
However, we could not find variables that were both easy to monitor
and well correlated with the actual cause of the delay. 


## Building a robust initial estimator

The "rate based" initial estimator requires estimating both the
data rate and the max RTT simultaneously. In contrast, the "CWND based"
initial estimator use in algorithms like Reno or Cubic
only requires estimating the CWND, plus a possibly
loose estimate of the data rate. The Reno algorithm is remarkably
simple: just increase the CWND by the number of bytes acknowledged,
without any explicit dependency on the measured latency.

The Reno algorithm terminates when packet losses are observed,
leading to bufferbloat. Hystart improves that by terminating when
the measured delays start increasing, but this can lead to early
exit in case of delay jitter. The rate based algorithm terminate when
the measured bandwidth stops growing, which provides good
results. Our proposal is to combine a Reno like growth of the
CWND with a rate-control like exit condition.

Of course, things are not that simple. The "rate" test only stops the
growth of the CWND after the third "non growing" round. If CWND doubles
after each round it becomes excessive, buffers fill up, and lots
of packets are lost. We dealt with that problem by essentially
freezing the increases of after the first "non growing" round.
If a larger measurement happens before 3 RTT, the increases
resume, otherwise, C4 exits the initial phase.

When the initial phase completes, we retain as estimate of the
data rate the highest value measured so far.
We also want to obtain a reasonable estimate of the "max RTT".
In the Reno logic, the "ssthresh" is set to half the CWND
value before congestion is detected. C4 will not use the
ssthresh variable after exiting the Initial phase, but it
can compute set the max RTT to the quotient of ssthresh by the
final rate estimate.

## Further work

The CWND based algorithm improves on the rate based algorithm
because it does not requires estimating the RTT of the connection.
It can absorb jitter events, and keep growing in the following RTT.





