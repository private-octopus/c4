# Addressing competition by tweaking the C4 cycle

Our analysis of the draft-4 version of C pointed a fairness issue.
That version of C4 would often grab more than 70% of the available
capacity, and sometimes more than 80%, which has a serious
impact on the competing connection. One of the plausible explanations
is that the cycle of Cruising, Probing, Recovery and Pushing is
too short, allowing C4 to quickly grab available bandwidth very
quickly when the competing connection slows down.

We first test that hypothesis by increasing the number of Cruising
RTT before Probing from 2 to 3, and running the tests. As shown
in the following tables, the results are not convincing:

|  top 90% load for compete tests| c4 | bbr | cubic | 3 cruising cycles |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% | 78% |
| vs_c4 |  52% | 31% | 33% | 52% |
| vs_cubic |  66% | 31% | 46% | 61% |
| after_c4 |  40% | 34% | 32% | 39% |
| before_c4 |  72% | 50% | 67% | 67% |
| vs_c4_lg |  61% | 58% | 58% | 61% |
| vs_c4_lg2 |  64% | 61% | 60% | 63% |
| vs_bbr_lg |  81% | 59% | 81% | 82% |
| vs_bbr_lg2 |  78% | 69% | 61% | 77% |
| vs_cubic_lg |  78% | 58% | 60% | 74% |
| vs_cubic_lg2 |  76% | 81% | 63% | 79% |

The longer cycle version is perhaps a bit less aggressive when competing
with Cubic, although not always. It seems just as aggressive when competing
with BBR. The longer cycle also makes C4 a bit less reactive in tests involving
changes in the network conditions. We try now another fix,
reducing the rate of growth in the pushing phase from 25% to 12.5%. As we
see in the next table, the results are not convincing:

|  top 90% load for compete tests| c4 | bbr | cubic | pushing 12.5% |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% | 78% |
| vs_c4 |  52% | 31% | 33% | 57% |
| vs_cubic |  66% | 31% | 46% | 68% |
| after_c4 |  40% | 34% | 32% | 43% |
| before_c4 |  72% | 50% | 67% | 69% |
| vs_c4_lg |  61% | 58% | 58% | 62% |
| vs_c4_lg2 |  64% | 61% | 60% | 62% |
| vs_bbr_lg |  81% | 59% | 81% | 81% |
| vs_bbr_lg2 |  78% | 69% | 61% | 78% |
| vs_cubic_lg |  78% | 58% | 60% | 79% |
| vs_cubic_lg2 |  76% | 81% | 63% | 76% |

There are probably some complex effects happening. For example, reducing
the Pushing rate increases the chance that C4 will remain longer in the
Pushing phase, increasing the pressure on the competing connection.
Next, we try reducing the probing rate from 6.25% to 3.125%.
The results are shown in the next table, and they are not convincing either:

|  top 90% load for compete tests| c4 | bbr | cubic | probing 3.125% |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 79% | 77% |
| vs_c4 |  52% | 31% | 33% | 56% |
| vs_cubic |  66% | 31% | 46% | 70% |
| after_c4 |  40% | 34% | 32% | 42% |
| before_c4 |  72% | 50% | 67% | 68% |
| vs_c4_lg |  61% | 58% | 58% | 61% |
| vs_c4_lg2 |  64% | 61% | 60% | 63% |
| vs_bbr_lg |  81% | 59% | 81% | 81% |
| vs_bbr_lg2 |  78% | 69% | 61% | 76% |
| vs_cubic_lg |  78% | 58% | 60% | 78% |
| vs_cubic_lg2 |  76% | 81% | 63% | 78% |

The good news is that probing at 3.125% only degrades the performance
of C4 in the other tests by a very small factor:

|  top 90% time for network events tests| c4 | bbr | cubic | probing 3.125% |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4589258 | 4700830 | 4551679 | 4775722 |
| alone_200 |  1183421 | 1222011 | 1148965 | 1188201 |
| alone_1_5M |  21511156 | 21718522 | 21552053 | 21481947 |
| alone_512k |  16173915 | 16217364 | 16210831 | 16211110 |
| low_and_up |  7570215 | 7516780 | 8071916 | 7608854 |
| drop_and_back |  7588592 | 7632647 | 7631565 | 7585498 |
| blackhole |  5592061 | 5814514 | 5699325 | 5609074 |
| short_long |  17538429 | 43393825 | 21553794 | 17543634 |
| satellite |  6807183 | 7432399 | 6704247 | 6807177 |

We might want to keep that last change, because it erases the difference
in behavior between paths that receive ECN feedback and paths that don't,
thus allowing us to simplify the code. But this exercise in trying to
tweak the growth cycle of C4 has only a negative conclusion. Coefficients
like number of Cruising cycles, Pushing growth rate and Probing growth rate
have very little impact on the aggressiveness of C4 when competing with other connections.
We have to look elsewhere for a solution to the fairness problem.

