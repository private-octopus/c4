# C4 - Draining Queues

The rsults of tests in buffer bloat scenarios show C4 creating shorter queues than
Cubic but longer queues than BBR, even if we implement the "trimming 1/2" changes:

|  average RTT for buffer bloat tests| c4 | bbr | cubic | trimming 1/2 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  113475 | 84750 | 229647 | 111173 |
| bbloat_c4 |  229418 | 92740 | 660442 | 234925 |
| bbloat_bbr |  131826 | 124558 | 266277 | 131812 |
| bbloat_cubic |  551053 | 97505 | 647602 | 551157 |

We first try to understand why by plotting the evolution of a C4 session
and a BBR session in a buffer bloat environment.

![graph comparing single C4 and BBR connections over bufferbloat](./c4-drain-excess-queues-c4-bbr-1.png "C4 and BBr in buffer bloat")

There are some poisitive lessons in this graph: both BBR and C4 converge to
an operating mode where the pacing rate matches the path capacity, and the RTT
matches the transmission delays of the path. C4, however, generates larger queues
than BBR in the start-up phase, and takes a longer time draining these queues.
We can see a couple of issues there:

* The C4 startup grows more rapidly than the BBR startup. This is partly
  by design, to overcome situations where start up could get stuck,
  but it might still be possible to be more cautious.

* The startup phase exits with a large "standing queue", which persists
  after the recovery phase.

* The queue is progressively reduced by reducing the "nominal max RTT"
  by a factor 1/8th per cycle, which in the simulation took 8 seconds.

We tested a simple change, adding a "draining" option to the
recovery phase. That option will be set if we detected a need to drain
during the previous cycle, which we set upon exceeding the Initial
phase, or if either the nominal rate or the nominal max RTT was
reduced during the previous cycle. If the draining option is set, the pacing
rate during recovery is set to only 7/8th of the nominal rate.

|  average RTT for buffer bloat tests| c4 | bbr | cubic | Trimming 1/2 | Trimm+Drain |
| --------- | ---:| ---:| ---:| ---:| ---:|
| bbloat |  113442 | 84752 | 229647 | 111175 | 99973 |
| bbloat_c4 |  232427 | 92729 | 660442 | 235607 | 194634 |
| bbloat_bbr |  132447 | 124744 | 266277 | 132141 | 113895 |
| bbloat_cubic |  551412 | 97494 | 647602 | 551335 | 546230 |

This simple change has a very positive effect on the average RTT for
the buffer bloat tests. It also appears to improve the fairness of
C4 during the compete tests:

|  top 90% load for compete tests| c4 | bbr | cubic | Trimming 1/2 | Trimm+Drain |
| --------- | ---:| ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% | 78% | 78% |
| vs_c4 |  52% | 21% | 33% | 53% | 52% |
| vs_cubic |  70% | 18% | 46% | 70% | 67% |
| after_c4 |  36% | 18% | 32% | 36% | 36% |
| before_c4 |  65% | 47% | 67% | 64% | 61% |
| vs_c4_lg |  54% | 21% | 58% | 55% | 54% |
| vs_c4_lg2 |  58% | 53% | 60% | 59% | 63% |
| vs_bbr_lg |  81% | 50% | 81% | 81% | 72% |
| vs_bbr_lg2 |  76% | 68% | 61% | 79% | 77% |
| vs_cubic_lg |  78% | 19% | 60% | 76% | 62% |
| vs_cubic_lg2 |  74% | 81% | 63% | 74% | 69% |
  
The fairness results improve across the board. We see a remarcable improvement
for the "vs_bbr_lg", for which the load passed from a worrying 81% to an
acceptable 71%.
(There may be a bug in the way we compute the load, and for some tests the load may be overestimated.)
The exception is the the "vs_cubic" test, which measure short competition with Cubic.

|  average time for wifi tests| c4 | bbr | cubic | Trimming 1/2 | Trimm+Drain |
| --------- | ---:| ---:| ---:| ---:| ---:|
| wifi_bad |  4070606 | 5442735 | 4134315 | 4235194 | 4206037 |
| wifi_fade |  5073239 | 5450844 | 5359530 | 5046484 | 5061633 |
| wifi_suspension |  4564955 | 4615857 | 4600733 | 4568762 | 4568861 |
| wifi_bad_bbr |  7065591 | 7448910 | 7909627 | 7067851 | 7625736 |
| wifi_bad_c4 |  8664120 | 9786873 | 8548230 | 8586032 | 8501670 |
| wifi_bad_cubic |  8649554 | 8826107 | 10820353 | 8964709 | 8795299 |

|  average load for wifi tests| c4 | bbr | cubic | Trimming 1/2 | Trimm+Drain |
| --------- | ---:| ---:| ---:| ---:| ---:|
| wifi_bad |  84% | 67% | 87% | 81% | 82% |
| wifi_fade |  87% | 77% | 80% | 87% | 87% |
| wifi_suspension |  91% | 89% | 91% | 91% | 91% |
| wifi_bad_bbr |  64% | 65% | 69% | 64% | 60% |
| wifi_bad_c4 |  51% | 48% | 55% | 52% | 52% |
| wifi_bad_cubic |  50% | 69% | 49% | 47% | 49% |

We seem to have a mild performance regression for the "wifi_bad" test --
draining does not compensate the performance regression introduced by
trimming.

For the "wifi_bad_bbr" test, we see a significant improvement in fairness,
and as a corrolary a mild performance regression.