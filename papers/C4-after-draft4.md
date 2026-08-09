# C4 after a year

We started the C4 project a year ago, with the goal of designing a congestion control algorithm that would be a good fit for real time applications sych as Media over QUIC.
We wanted the algorithm to be simple and easy to implement, while performing well in the range of environments found for real time applications.

Our initial idea was to develop an algorithm that tracked the evolution of the round trip time, backs off when it detects increased delays, and avoids the building of queues.
We quickly evolved that design after encountering two majot issues: the need to compete with algorithms like Cubic that do not reactto delay increases;
and, perhaps more importantly, the need to work well in Wi-Fi networks that often exhibit a high amount of "delay jitter".
The resulting design focused on tracking the path bandwidth and the maximum RTT.

The nominal path bandwidth is tracked by measuring the rate of data delivery over the connection,
and lowering that nominal rate when congestion is detected.
Most of the time, C4 paces transmission at the nominal rate to avoid overloading the network,
but C4 will periodically probe at a slightly higher rate to detect the possibility of
bandwidth increase.
If the probe is positive, C4 will entre a "pushing" state and try to rapidly
increase the data rate.

The nominal maximum RTT tracks the maximum RTT values observed when C4 is sending data at or
below the nominal rate. The congestion window will normally be set to the product of that
nominal max RTT and the pacing rate, allowing for continuous transmission at the set
rate even if delay jitter is present. The nominal max RTT is allowed to decay over time,
slowly converging to the most recent RTT measurements.

The details of the algorithm are presented in the [C4 protocol draft](https://datatracker.ietf.org/doc/draft-huitema-ccwg-c4-spec/).
A companion [C4 design draft](https://datatracker.ietf.org/doc/draft-huitema-ccwg-c4-design/)
documents the design process, and a [C4 test draft](https://datatracker.ietf.org/doc/draft-huitema-ccwg-c4-tests/).
The version 4 of these drafts was presented at the last IETF meeting in Vienna
(see [slides](https://datatracker.ietf.org/meeting/126/materials/slides-126-ccwg-updating-c4-christians-cc-code-00)).

To validate the C4 design, we carry a series of simulations, corresponding to 38 different scenarios.
Each simulation is repeated 100 times. In the C4 test draft, we presented the average top 90% execution
time for each of these tests, and compared them to the corresponding results when running
BBR or Cubic instead. The results are encouraging. If we consider the "average" performance, C4 is the best
in 22 out of 30 tests, and arrives as second best in the 8 other tests.
If we consider the "90% worst"C4 does better in 21 out of 30 "performance"
test, and arrives as second best in the 9 other tests. We find only 2 tests where there is
a big difference betwen C4 and the best performing algorithm: BBR wins the competition
with Cubic in the "vs_cubic_lg2" test, and Cubic that 10% better than BBR for the top 90%
worst case of the "bad_wifi" test. We find similar patterns when measuring the frame
latency in media tests.

Despite these good results, we concluded the IETF presentations by listing two issues:
we should test additional scenarios, including some that could elicit bufferbloat,
and we should consider other metrics that raw performance. As a first step, we started
evaluating the RTT and its variations, as well as the load imposed by C4 on the network.

Tracking the average RTT gives us an indication of the amount of queuing happening in the
network. However, we want to also evaluate the impact of RTT variations, to give
us an idea of the transient queues. We could not directly use the max RTT, because
this is typically encountered in the "startup" phase of the connection, and is not representative
of the steady state behavior of the connection. Instead, we decided to track the sum of the
average RTT and the standard deviation of the RTT, which if the RTT distribution were normal,
would give us the 84% worst case RTT, and in any case gives us a better indication than just
tracking the average. The table below show that metric for the "network events" tests. 

|  top 90% of RTT + standard deviation for network events tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| alone |  145798 | 107604 | 167571 |
| alone_200 |  88677 | 57168 | 70772 |
| alone_1_5M |  96662 | 64683 | 90867 |
| alone_512k |  105400 | 104226 | 106116 |
| low_and_up |  127913 | 134293 | 147216 |
| drop_and_back |  148289 | 150347 | 161333 |
| blackhole |  479783 | 500638 | 485330 |
| short_long |  230005 | 232484 | 409884 |
| satellite |  602366 | 830684 | 621278 |

We see that C4 provides the best result in 5 out of 9 tests, is second best in 2 other tests,
but is clearly worse in 2 of the tests: a short 200 Mbps connection, and a low bandwidth
1.5 Mbps connection. That's clearly an area for improvement.

We track the load imposed to the network by measuring the volume of packets sent over
the network over the duration of the connection and dividing that by the duration of the connection.
This ratio is not very interesting in stand alone tests, because tracking the test duration
already measures how efficiently C4 uses the available bandwidth. However, it is more interesting
in tests where several connections compete. In those cases, the load shows the fraction of
the network capacity used by C4, from which we can infer the capacity left for other connections.
The two table belows show the average and top 90% load imposed by C4, BBR and Cubic in the "compete" tests,
and how it compares with BBr or C4 on the same tests

|  average load for compete tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 78% |
| vs_c4 |  51% | 31% | 30% |
| vs_cubic |  64% | 30% | 41% |
| after_c4 |  36% | 33% | 31% |
| before_c4 |  67% | 42% | 58% |
| vs_c4_lg |  60% | 41% | 54% |
| vs_c4_lg2 |  63% | 60% | 59% |
| vs_bbr_lg |  80% | 59% | 80% |
| vs_bbr_lg2 |  76% | 68% | 60% |
| vs_cubic_lg |  70% | 58% | 60% |
| vs_cubic_lg2 |  74% | 80% | 62% |


|  top 90% load for compete tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% |
| vs_c4 |  52% | 31% | 33% |
| vs_cubic |  66% | 31% | 46% |
| after_c4 |  40% | 34% | 32% |
| before_c4 |  72% | 50% | 67% |
| vs_c4_lg |  61% | 58% | 58% |
| vs_c4_lg2 |  64% | 61% | 60% |
| vs_bbr_lg |  81% | 59% | 81% |
| vs_bbr_lg2 |  78% | 69% | 61% |
| vs_cubic_lg |  78% | 58% | 60% |
| vs_cubic_lg2 |  76% | 81% | 63% |

According to [RFC9743](https://datatracker.ietf.org/doc/html/rfc9743),
_"A proposed congestion control algorithm that has a significantly negative
impact on flows using standard congestion control might be suspect, and this
aspect should be part of the community's decision making with regards to the
suitability of the proposed congestion control algorithm."_ Impact here
encompasses causing packet losses or long queues, or simply consuming
too much of the available bandwidth. Using less than 50% of the capacity would
be considered good, using more than 70% would be considered bad, and more
than 80% really bad.

The tables show that C4 often consume more than 70% of the available capacity,
and sometimes more than 80%. That's definitely not ideal, and is another
area for improvement.

Another recommendation in [RFC9743](https://datatracker.ietf.org/doc/html/rfc9743)
is that algorithms _"ought to try to avoid maintaining excessive queues in the network"_,
which is then developed as the need to somehow address "buffer bloat". To start
addressing that recommendation, we added a series of "buffer bloat" tests to
our set of simulations. The following tables present to "top RTT" and "top 90% load" 
in these tests.

|  top 90% of RTT + standard deviation for buffer bloat tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| bbloat |  158066 | 95924 | 322642 |
| bbloat_c4 |  361542 | 118202 | 939834 |
| bbloat_bbr |  160655 | 159931 | 408664 |
| bbloat_cubic |  859970 | 137306 | 1039625 |

|  top 90% load for buffer bloat tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| bbloat |  98% | 95% | 98% |
| bbloat_c4 |  59% | 58% | 60% |
| bbloat_bbr |  82% | 58% | 86% |
| bbloat_cubic |  60% | 58% | 60% |

The results show that C4 as in draft 4 contains the RTT somewhat -- definitely
not as well as BBR, but better than Cubic. They also show that while competition with
itself or with Cubic is handled fairly in buffer boat conditions, competition
with BBR is rather unfair, probably because C4 generates much larger queues than
BBR can tolerate.

The next months will be busy improving C4 in these two areas: try to
reduce the RTT variations, and try to improve fairness.