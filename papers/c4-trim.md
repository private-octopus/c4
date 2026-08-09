# Reduce the send rate to reduce queues

The large delays that we observe are caused by increased queues, which are caused by
sending data too fast for too long. Tweaking the max RTT solves the "too long" part of the
issue, but there are limits because the long nominal max RTTs are necessary in high
jitter situations. In fact, we do see cases where delay jitter causes the data rate
estimate to be slightly higher than the actual available bandwidth.
To reduce the queues, we proably need C4 to send a bit slower.

We tried a simple change: when the data rate does not increase for a whole cycle, and the
transmission in the probing stage was not application limited, reduce the
nominal data rate to track the measurements -- something that C4 only did
before when noticing persistent congestion. We experimented with different rates of decrease,
and found that a 1/2th decrease provided the most interesting results. The change in
specification would be, "if the data rate does not increase for a whole cycle,
and the transmission in the probing stage was not application limited,
reduce the nominal data rate to the average of the previous value and the
highest measurement in the last cycle."

|  top 90% of RTT + standard deviation for network events tests| c4 | bbr | cubic | Trimming 1/2 rate |
| --------- | ---:| ---:| ---:| ---:|
| alone |  145798 | 107604 | 167571 | 141307 |
| alone_200 |  88677 | 57168 | 70772 | 88272 |
| alone_1_5M |  96662 | 64683 | 90867 | 73264 |
| alone_512k |  105400 | 104226 | 106116 | 90963 |
| low_and_up |  127913 | 134293 | 147216 | 129420 |
| drop_and_back |  148289 | 150347 | 161333 | 149161 |
| blackhole |  479783 | 500638 | 485330 | 432236 |
| short_long |  230005 | 232484 | 409884 | 229986 |
| satellite |  602366 | 830684 | 621278 | 602326 |

The top RTT values are lower in all the
network events tests but two, the "low and up" test, with a difference of
1.4 ms, and the "drop and back" test, with a difference of 1.1 ms.
We see significant improvement in the 1.5 Mbps test, with a difference of 23 ms,
and in the 512 Kbps test, with a difference of 14 ms. We seem to see a
comromise: trimming the nominal rate quickly does reduce the queues in
general, but having a slightly lower nominal rate means that queues will
persist a bit longer after a rate increase. The tables of "time spent" do not
show any preformance decrease in these tests -- some are faster, some are slower, but
the differences are tiny.

|  top 90% of RTT + standard deviation for buffer bloat tests| c4 | bbr | cubic | Trimming 1/2 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  158066 | 95924 | 322642 | 156178 |
| bbloat_c4 |  361542 | 118202 | 939834 | 345468 |
| bbloat_bbr |  160655 | 159931 | 408664 | 159903 |
| bbloat_cubic |  859970 | 137306 | 1039625 | 859395 |

|  top 90% load for buffer bloat tests| c4 | bbr | cubic | Trimming 1/2 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  98% | 95% | 98% | 98% |
| bbloat_c4 |  59% | 58% | 60% | 59% |
| bbloat_bbr |  82% | 58% | 86% | 82% |
| bbloat_cubic |  60% | 58% | 60% | 60% |

Trimming has very little effect on the buffer bloat tests. We do see some small
reductions in the top RTT for some tests, but these are too small to matter.
We also do not see any big change in the fairness of copeting under buffer bloat.
One possibility is that, while trimming prevents the queues from increasing
too much, it does not by itself drain them. We should probably complement
trimming by some explicit form of draining.

|  top 90% of RTT + standard deviation for compete tests| c4 | bbr | cubic | Trimming 1/2 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  127906 | 156408 | 163495 | 125994 |
| vs_c4 |  147578 | 133587 | 154995 | 162418 |
| vs_cubic |  161612 | 151990 | 165428 | 160008 |
| after_c4 |  150671 | 129600 | 139090 | 150557 |
| before_c4 |  109443 | 96827 | 110039 | 108681 |
| vs_c4_lg |  147437 | 112710 | 153852 | 149633 |
| vs_c4_lg2 |  158254 | 154662 | 141550 | 157719 |
| vs_bbr_lg |  141010 | 160019 | 162743 | 137970 |
| vs_bbr_lg2 |  147211 | 155612 | 149919 | 154808 |
| vs_cubic_lg |  151434 | 120204 | 162934 | 150852 |
| vs_cubic_lg2 |  146843 | 149529 | 159656 | 146035 |

|  top 90% load for compete tests| c4 | bbr | cubic | Trimming 1/2 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% | 78% |
| vs_c4 |  52% | 31% | 33% | 54% |
| vs_cubic |  66% | 31% | 46% | 69% |
| after_c4 |  40% | 34% | 32% | 38% |
| before_c4 |  72% | 50% | 67% | 69% |
| vs_c4_lg |  61% | 58% | 58% | 61% |
| vs_c4_lg2 |  64% | 61% | 60% | 63% |
| vs_bbr_lg |  81% | 59% | 81% | 81% |
| vs_bbr_lg2 |  78% | 69% | 61% | 80% |
| vs_cubic_lg |  78% | 58% | 60% | 77% |
| vs_cubic_lg2 |  76% | 81% | 63% | 75% |

Trimming the nominal rate quickly does not seem to improve the fairness of C4 in the compete tests,
but it does improve the top RTT measurements in the competition between C4 and BBR, and between
C4 and Cubic, except for the "vs_bbr_lg2" test, where the top RTT is 7.6 ms higher than before.
We also see a slight degradation in the "internal competition" tests, where C4 competes with itself.

|  top 90% time for wifi tests| c4 | bbr | cubic | Trimming 1/2 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  4841982 | 7522796 | 4449644 | 5249372 |
| wifi_fade |  5351680 | 5649668 | 5550500 | 5244862 |
| wifi_suspension |  4574262 | 4616911 | 4602102 | 4572601 |
| wifi_bad_bbr |  11014365 | 10730711 | 13129404 | 10454091 |
| wifi_bad_c4 |  11665740 | 12481659 | 12241907 | 11615184 |
| wifi_bad_cubic |  11883994 | 12246412 | 13881720 | 11388974 |

|  top 90% of RTT + standard deviation for wifi tests| c4 | bbr | cubic | Trimming 1/2  |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  236990 | 146354 | 149225 | 260841 |
| wifi_fade |  210684 | 177334 | 187572 | 210766 |
| wifi_suspension |  32878 | 34220 | 45312 | 28783 |
| wifi_bad_bbr |  335207 | 339723 | 327846 | 332603 |
| wifi_bad_c4 |  331313 | 333343 | 333270 | 331021 |
| wifi_bad_cubic |  339101 | 357546 | 327358 | 335187 |

We avoided making similar changes before because of the effect on high jitter situations,
in particular "bad wifi" environments. This is indeed an area of concern. The measurements
show that for the "wifi_bad" test, the "top 90%" execution time increases by 400ms, about 10%,
and the top RTT is 24 ms higher than before, again about 10%. On the other hand, the
measurements for competition in bad wifi condition improve of remain stable. The
decreased preformance are probably due to the same factor found in the
the "low and up" and the "drop and back" test: trimming the nominal rate quickly reduces
the queues in general, but may cause queues to drain slower in high jitter situations.

Overall, this change seems to be a good compromise. There is just one important drawback
in the "bad wifi" scenario. We might be able to address it by having a more subtle
rule for the "trimming" logic, maybe not trimming so quickly if we recognize a high jitter
scenario. This will require a separate investigation. Analysis of the
buffer bloat scenario also showed that trimming ought to be complemented by
a more forceful way to drain existing queues.

