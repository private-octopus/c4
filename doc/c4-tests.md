---
title: "Testing of Christian's Congestion Control Code (C4)"
abbrev: "C4 Tests"
category: info

docname: draft-huitema-ccwg-c4-test-04
submissiontype: IETF
number:
date:
consensus: true
ipr: trust200902
area: "Web and Internet Transport"
keyword:
 - C4
 - Congestion Control
 - Realtime Communication
 - Media over QUIC

author:
 -
   ins: C. Huitema
   name: Christian Huitema
   org: Private Octopus Inc.
   email: huitema@huitema.net
 -
   ins: S. Nandakumar
   name: Suhas Nandakumar
   organization: Cisco
   email: snandaku@cisco.com
 -
   ins: C. Jennings
   name: Cullen Jennings
   organization: Cisco
   email: fluffy@iii.ca

normative:
informative:
   RFC9000:
   I-D.ietf-moq-transport:
   RFC9959:
   RFC9743:

   Picoquic:
    target: https://https://github.com/private-octopus/picoquic
    title: "Picoquic"
    date: "2025"
    seriesinfo: "GitHub Repository"
    author:
    -
      ins: C. Huitema

   Picoquic_ns:
    target: https://https://github.com/private-octopus/picoquic_ns
    title: "Picoquic Network Simulator"
    date: "2025"
    seriesinfo: "GitHub Repository"
    author:
    -
      ins: C. Huitema
   
--- abstract

Christian's Congestion Control Code is a new congestion control
algorithm designed to support Real-Time applications such as
Media over QUIC. It is designed to drive towards low delays,
with good support for the "application limited" behavior
frequently found when using variable rate encoding, and
with fast reaction to congestion to avoid the "priority
inversion" happening when congestion control overestimates
the available capacity. The design was validated by
series of simulations, and also by initial deployments
in control networks. We describe here these simulations and
tests.

--- middle

# Introduction

Christian's Congestion Control Code (C4) is a new congestion control
algorithm designed to support Real-Time multimedia applications, specifically
multimedia applications using QUIC {{RFC9000}} and the Media
over QUIC transport {{I-D.ietf-moq-transport}}. The design was validated by
series of simulations, and also by initial deployments
in control networks. We describe here these simulations (see {{simulations}}),
the simulation results for each of the test cases (see {{results}}),
and the live networking tests (see {{live-tests}}).

# Description of simulation tests  {#simulations}

We test the design by running a series of simulations, which cover:

* reaction to network events

* competition with other congestion control algorithms

* handling of high jitter environments

* handling of multimedia applications

* handling of ECN

We are running the tests using the picoquic network simulator {{Picoquic_ns}}.
The simulator embeds the picoquic implementation of QUIC {{Picoquic}}.
Picoquic itself comes with support for a variety of congestion control
protocols, including Cubic and BBR. We added an implementation of C4.

That implementation is designed so that the same code can be used
in execution over the network and in simulations, the main difference being
a replacement of the socket API by a simulation API. When running in
simulation, the code runs in "virtual time", with a virtual clock driven
by simulation events such as arrival and departure of packets from
simulated queues. With the virtual clock mechanism, we can simulate
in a fraction of a second a connection that would last 10 seconds in "real time".

## Reaction to network events

The first series of simulation test how C4 behaves in simple scenarios
when it is the sole user of a link. The list of test includes:

* a 20Mbps connection,
* a 200Mbps connection,
* a geostationary satellite connection,
* a sudden increase in path capacity, i.e. "low and up"
* a sudden decrese in path capacity followed by a return to normal, i.e. "drop and back"
* a sudden drop to 0 of path capacity for 2 seconds, i.e. a "black hole"
* a sudden increase in path latency, from "short" to "long"

### Simulation of a simple 20Mbps connection (alone)

This scenario simulates a 10MB download over a 20 Mbps link,
with an 80ms RTT, and a bottlneck buffer capacity corresponding
to 1 BDP.

In a typical simulation, we see a initial phase complete in less
than 800ms, followed by a recovery phase in which the
transmission rate stabilizes to the line rate. After that,
the RTT remains very close to the path RTT, except for
periodic small bumps during the "push" transitions.

### Simulation of a simple 200Mbps connection (alone_200)

This scenario simulates a 20MB download over a 200 Mbps link,
with a 40ms RTT, and a bottleneck buffer capacity corresponding
to 1 BDP.

This short test shows that the initial phase correctly discover
the path capacity, and that the transmission operates at
the expected rate after that.


### Simulation of a simple 1.5Mbps connection (alone_1_5M)

This scenario simulates a 4MB download over a 1.5 Mbps link,
with a 40ms RTT, and a bottleneck buffer capacity corresponding
to 1.25 BDP.

This short test shows that the initial phase correctly discover
the path capacity, and that the transmission operates at
the expected rate after that.

### Simulation of a simple 512kbps connection (alone_512k)

This scenario simulates a 1MB download over a 512 kbps link,
with a 34ms RTT, and a bottleneck buffer capacity corresponding
to 50ms of transmission.

This short test shows that the initial phase correctly discover
the path capacity, and that the transmission operates at
the expected rate after that.

### Low and up

The "low and up" scenario simulates a sudden increase in the
capacity of the path. At the beginning of the simulation,
the simulated bandwidth is set at 5 Mbps. It increases to
10 Mbps after 2.5 seconds. The RTT remains constant at
100ms.

The goal of the test is to verify that C4 promptly
discovers the increase in bandwidth, and
increases the transmission rate.

### Drop and back

The "drop and back" scenario simulates a sudden decrease in the
capacity of the path, followed by return to normal.
At the beginning of the simulation,
the simulated bandwidth is set at 10 Mbps. It decreases
to 5 Mbps after 1.5 second, then returns to 10 Mbps
after 2 seconds. The RTT remains constant at
100ms.

The goal of the test is to verify that C4 adapts
promptly to changes in the available bandwidth on a
path.

### Black Hole

The "black hole" scenario simulates a sudden decrease in the
capacity of the path, followed by return to normal.
At the beginning of the simulation,
the simulated bandwidth is set at . After 2 seconds,
the path capacity is set to 0, and is restored to normal
2 seconds later. The RTT remains constant at
70ms.

The goal of the test is to verify that C4 recovers
promptly after a short suspension of the path.

### Short and long

The "short and long" scenario simulates a sudden increase in the
latency of the path.
At the beginning of the simulation,
the simulated RTT is set at 30ms. After 1 second, the
latency increases to 100ms. The data rate remains constant at
100ms.

The goal of the test is to verify that C4 react properly
exercises the "slow down" mechanism to discover the new RTT.

### Simulation of a geostationary satellite connection (satellite)

This scenario simulates a 100MB download over a 250 Mbps link,
with a 600ms RTT, and a bottleneck buffer capacity corresponding
to 1 BDP, i.e., simulating a geostationary satellite connection.
The scenario also tests the support for careful resume
{{RFC9959}} by setting
the remembered CWND to 18750000 bytes and the
remembered RTT to 600.123ms.


## Competition

In accordance with {{RFC9743}}, we evaluate competition between
C4 connections, or between C4 and Cubic or BBR. We design a series of tests,
each correponding to a competition scenario between a "main" connection and
a "background" connection. For each test, we run the test using either C4,
Cubic or BBR for the "main" connection. The test scenario specifies the
algorithm managing the background connection, as well as scenario details.




we design series of tests
of multiple competing flows all using C4. We want to test
different conditions, such as data rate and latency,
and also different scenarios, such as testing whether
the "background" connection starts at the same time, before
or after the "main" connection.

We test that the bandwidth is shared reasonably by testing
the completion time of a download, and setting the target
value so it can only be achieved if the main connection
gets "about half" of the bandwidth.

### Short main connection versus C4 (vs_c4)

Our first test simulates a main connection starting at the
same time as a background C4 connection. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 10MB, the main connection downloads 5MB.

### Short background C4 connection first (after_c4)

The "background first" test simulates a main connection competing
with the background C4 connection that started
0.5 seconds before the main connection. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 10MB, the main connection downloads 5MB.

### Short background C4 connection last (before C4)

The "background last"  simulates a main connections competing
with the background connection that starts
0.5 seconds after the main connection. The path has a 50Mbps data rate
and 30ms RTT. The background connection
tries to download 20MB, the main connection downloads 10MB.

### Two long connections 

The long connection test simulates a main connections starting at the
same time as the background. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 30MB, the main connection downloads 20MB.

There are three variants of that test, depending on the background
connection algorithm: C4 (vs_c4_lg), Cubic (vs_cubic_lg) or BBR
(vs_bbr_lg).

### Long background connection last

The long "background last" test simulates a main connections competing
with the background connection starting
1 second after it. The path has a 10Mbps data rate
and 70ms RTT. The background connection
tries to download 15MB, the main connection downloads 10MB.

There are three variants of that test, depending on the background
connection algorithm: C4 (vs_c4_lg2), Cubic (vs_cubic_lg2) or BBR
(vs_bbr_lg2).


## Handling of High Jitter Environments {#c4-wifi}

In the design of C4, we have been paying special attention to
"bad Wi-Fi" environments, in which the usual delays of a few
milliseconds could spike to 50 or even 200ms. We spent a lot of time trying to
understand what causes such spikes. Our main hypothesis is that
this happens when multiple nearby Wi-Fi networks operate on the
same frequency or "channel", which causes collisons due to the
hidden node problem. This causes collisions and losses, to which
Wi-Fi responses involves two leves of exponential back-off.

We built a model to simulate this jitter by combining two generators:

* A random value r between 0 and 1 ms to model collision avoidance,
* A Poisson arrival model with lambda=1 providing the number N1 of short scale 1ms intervals
  to account for collision defferal and retry,
* A Poisson arrival arrival model with lambda = 12,
  and an interval length of 7.5ms to account for Wi-Fi packet restransmission.

We combine these generators models by using a coefficient "x" that indicates the general
degree of collisions and repetitions:

* For a fraction (1-x) of the packets, we set the number N2 to 0.
* For a fraction (x) of the packets, we compute N2 from the Poisson arrival model with lambda = 12,
  and an interval length of 7.5ms.

The latency for a single sample will be:
~~~
latency = N1*1ms + N2*7.5ms
if N1 >= 1:
    latency -= r
~~~
The coefficient x is derived from the target average jitter value. If the target is
1ms or less, we set x to zero. If it is higher than 91ms, we set x to 1. If
it is in between, we set:
~~~
x = (average_jitter - 1ms)/90ms
~~~
We have been using this simulation of jitter to test our implementation of multiple
congestion control algorithms.

### Bad Wi-Fi test {#bad-wifi}

The "bad Wi-Fi" test simulates a connection experiencing a high level of
jitter. The average jitter is set to 7ms, which implies multiple spikes
of 100 to 200ms every second. The data rate is set to 10Mbps, and the base
RTT before jitter is set to 2ms, i.e., simulating a local server.

### Wifi fade trial {#wifi-fade}

The "Wi-Fi fade" trial simulates varying conditions. The connection starts
with a data rate of 20Mbps, an 80ms latency, and Wi-Fi jitter
with average 1ms. After 1 second, the data rate drops to 2Mbps
and the jitter average increases to 12ms. After another 2 seconds,
data rate and jitter return to the original condition.

### Wifi suspension trial {#wifi-suspension}

The "Wi-Fi suspension" test simulates a connection experiencing
multiple "suspensions". For every 1.8 second of a 2 second interval,
the data rate is set to 20Mbps, and the base
RTT before jitter is set to 10ms. For the last 200ms of these
intervals, the data rate is set to 0. This model was developed
before we got a better understanding of the Wi-Fi jitter. It is
obsolete, but we kept it as a test case anyhow.

### Compete over bad Wi-Fi

The "compete over bad Wi-Fi" test simulates a main connection using 
a "bad Wi-Fi" path and competing on the same path with a background
connection, with the main
connection starting 1 second after the background connection.
The path has a 10Mbps data rate and 2ms RTT, plus Wi-Fi jitter
set to 7ms average -- 
the same jitter characteristics as in the "bad Wi-Fi" test (see {{bad-wifi}}).
The background connection
tries to download 10MB, the main connection downloads 4MB.

There are three variants of that test, depending on the background
connection algorithm: C4 (wifi_bad_c4), Cubic (wifi_bad_cubic) or BBR
(wifi_bad_bbr).

## L4S and ECN {#ecn-simulations}

To evaluate the handling of ECN, we run a series of tests in which the
bottleneck queue is managed by the "duaQ" adaptie queue management
algorithm (AQM) specified for L4S {{RFC9743}}

### Basic ECN test (ecn) {#ecn-test}

The "ECN" test simulates a 20 Mbps link,
with an 80ms RTT, and a bottleneck buffer capacity corresponding
to 1 BDP. 

When using C4 we set the ECT1 marking, signaling support
of L4S. We do not set these markings when using Cubic or BBR.

### Competition with other algorithms

The "compete over ECN" tests simulates a main connection competing
against a background connection, using the same network path characteristics
as the "ECN" test (see {{ecn-test}}).

There are three variants of this test, with the background connection using
either C4 (ecn_c4), Cubic (ecn_cubic) or BBR (ecn_bbr). 


## Handling of Multimedia Applications

C4 is specifically designed to properly handle multimedia applications. We test
that function by running simulations of a call including:

* a simulated audio stream sending 80 bytes simulated audio segments every 20 ms.
* a simulated compressed video stream, sending 30 frames per second, organized
  as groups of 30 frames each starting with a 37500 bytes simulated I-Frame
  followed by 149 3750 bytes P-frames.
* a simulated less compressed video stream, sending 30 frames per second, organized
  as groups of 30 frames each starting with a 62500 bytes simulated I-Frame
  followed by 149 6250 bytes P-frames.

The simulation sends each simulated audio segment as QUIC datagram, with
QUIC priority 2, and each group of frames as a separate QUIC stream with priority
4 for the compressed stream, and a priority 6 for the less compressed stream.

If the frames delivered on the less compressed stream fall are delivered
more than 250ms later than the expected time, the receiver sends a "STOP SENDING"
request on the QUIC stream to cancel it; transmission will restart with
the next group of frame, simulating a plausible "simulcast" behavior.

The simulator collects statistics on the delivery of media frame, which are
summarized as average and maximum frame delivery delay. For each test, the
simulation specifies an expected average and an expected maximum delay, as
well as a "start measurement" time, typically set long enough to start after
the initial "startup" phase. The
test passes if the average and max value for the simulated audio and for
the simulated compressed video measured after the start time
are below the specified values.

### Media on High Speed Connection (media)

The "media" test verifies simulates the handling of media on a 100 Mbps
connection with a 30ms RTT. The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection. 

### Media on 10 Mbps Connection (media10)

The "media10" test verifies the handling of media on a 10 Mbps
connection with a 40ms RTT.  The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection.

### Media for 20 seconds (media600)

The "media600" media checks that media performance does not
degrade over time, simulating a 100Mbps connection with a 30ms RTT.
The test lasts for 20 video groups of frames, i.e. 20 seconds. 
The measurements start 200ms after the
start of the connection.

### Media over varying RTT (media_short_long)

The "media_short_long" media test verifies that media performance does not
degrade over time, simulating a 100Mbps connection with a 30ms RTT,
that changes to a 100ms RTT after 1 second.
The test lasts for 10 video groups of frames, i.e. 10 seconds. 
The measurements start 5 seconds after the
start of the connection.

### Media over bad Wi-Fi (media_wb)

The "bad Wi-Fi" media test verifies that media performance does not
degrade too much on a connection that has the kind of jitter
discussed in {{c4-wifi}}. The connection has the characteristics
similar to the "bad Wi-Fi" scenario described in {{bad-wifi}}.
The average jitter is set to 7ms, which implies multiple spikes
of 100 to 200ms every second. The data rate is set to 20Mbps, and the base
RTT before jitter is set to 2ms, i.e., simulating a local server.
The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection.

### Media over fading Wi-Fi (media_wf)

The "fading Wi-Fi" media test verifies that media performance does not
degrade too much on a connection that hast characteristics
similar to the "fading Wi-Fi" scenario described in {{wifi-fade}}.
The connection starts
with a data rate of 20Mbps, 40ms RTT, and Wi-Fi jitter
with average 1ms. After 1 second, the data rate drops to 2Mbps
and the jitter average increases to 12ms.
The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection.

### Media with Wi-Fi suspensions (media_ws)

The "varying Wi-Fi" media test verifies that media performance does not
degrade too much on a connection experiences suspensions as
discussed in {{wifi-suspension}}.
For every 1.8 second of a 2 second interval,
the data rate is set to 20Mbps, and the base
RTT before jitter is set to 10ms. For the last 200ms of these
intervals, the data rate is set to 0.
The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection.

### Media over an ECN capable connection (media_ecn)

The "varying Wi-Fi" media test verifies that media works as expected
on a path managed using ECN/L4S. The set up is similar to the "ECN" test
discussed in {{ecn-simulations}}.

# Simulation results {#results}

Simulations include random events, such as network jitter or the
precise timing of packet arrivals and departure. Minute changes in starting
conditions can have cascading effects. To get reliable results, we run each test 100
times. The simulator produces a log of each test execution (in QLOG format), and a summary
of each test results, including the completion time for each test, and for tests
checking media the average and max frame delivery time.

We present here a summary of the results, including the average and the 90th percentile
of the completion time for each test. For media tests, we also report the average frame
delivery time and the 90th percentile of the max frame delivery time.

We run these tests for C4, Cubic and BBR, and present the results for these 3
congestion control algorithms in a set of tables. All times are expressed in microseconds,
and for all results lower time values are considered better.

## Reaction to network events

Here are the statistics for the network events test cases.

###  average time for network events tests

|  average time for network events tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4502913 | 4689260 | 4472465 | 4642195 |
| alone_200 |  1115776 | 1221630 | 1145722 | 1161980 |
| alone_1_5M |  21504710 | 21717251 | 21514264 | 21660915 |
| alone_512k |  16173870 | 16211371 | 16183314 | 16213861 |
| low_and_up |  7569237 | 7506849 | 8035433 | 7762235 |
| drop_and_back |  7554195 | 7625693 | 7629764 | 7697371 |
| blackhole |  5591981 | 5811316 | 5695660 | 5628028 |
| short_long |  17536781 | 42331541 | 21368101 | 17537092 |
| satellite |  6807127 | 7492539 | 6704246 | 6807111 |

###  top 90% time for network events tests

|  top 90% time for network events tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4564480 | 4698415 | 4518852 | 4835141 |
| alone_200 |  1181668 | 1222012 | 1148423 | 1186067 |
| alone_1_5M |  21511156 | 21718512 | 21552321 | 21661024 |
| alone_512k |  16173974 | 16217210 | 16208261 | 16215577 |
| low_and_up |  7570221 | 7511647 | 8071920 | 7764215 |
| drop_and_back |  7579428 | 7630825 | 7632455 | 7698289 |
| blackhole |  5592061 | 5815444 | 5699327 | 5628156 |
| short_long |  17538429 | 43394841 | 21541922 | 17538424 |
| satellite |  6807174 | 7834142 | 6704247 | 6807137 |


## Competition

Here the statistics for the compete test cases.

###  average time for compete tests

|  average time for compete tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2817663 | 4501471 | 2853022 | 2964582 |
| vs_c4 |  4361871 | 6813101 | 7891899 | 4490594 |
| vs_cubic |  3428960 | 6974953 | 5348004 | 3484869 |
| after_c4 |  6563029 | 6846566 | 7208456 | 5239798 |
| before_c4 |  2640670 | 4281776 | 3105136 | 2699206 |
| vs_c4_lg |  21026786 | 32250064 | 23618741 | 21067859 |
| vs_c4_lg2 |  20979188 | 21139542 | 21818194 | 21102894 |
| vs_bbr_lg |  15612556 | 21098503 | 15562778 | 16742530 |
| vs_bbr_lg2 |  16449739 | 18711270 | 21520837 | 20600335 |
| vs_cubic_lg |  17902039 | 21430554 | 20902893 | 17578391 |
| vs_cubic_lg2 |  17080952 | 15533300 | 20672401 | 16969990 |

###  top 90% time for compete tests

|  top 90% time for compete tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2824981 | 4580449 | 2877804 | 2983881 |
| vs_c4 |  4453585 | 6843240 | 8424848 | 4864821 |
| vs_cubic |  3761300 | 7089722 | 5580459 | 3555684 |
| after_c4 |  6742984 | 6991485 | 7494092 | 6102901 |
| before_c4 |  2734698 | 5404668 | 4163561 | 3001428 |
| vs_c4_lg |  21139706 | 39556964 | 25105527 | 21141447 |
| vs_c4_lg2 |  21046812 | 21379953 | 22272580 | 21174182 |
| vs_bbr_lg |  15808681 | 21131562 | 15839671 | 16936214 |
| vs_bbr_lg2 |  16522592 | 18954745 | 22323666 | 21138531 |
| vs_cubic_lg |  20251838 | 21760143 | 21120555 | 18440982 |
| vs_cubic_lg2 |  17419617 | 15706948 | 20930258 | 17548782 |


## Wi-Fi

Here the statistics for the wifi test cases.

###  average time for wifi tests

|  average time for wifi tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  4059372 | 5601202 | 4076699 | 4144883 |
| wifi_fade |  5065021 | 5403001 | 5341227 | 5203858 |
| wifi_suspension |  4564740 | 4615871 | 4600118 | 4563252 |
| wifi_bad_bbr |  7582895 | 7280777 | 6837401 | 7581238 |
| wifi_bad_c4 |  8750784 | 9650917 | 8426742 | 9347050 |
| wifi_bad_cubic |  8618719 | 8731338 | 10397119 | 8407363 |

###  top 90% time for wifi tests

|  top 90% time for wifi tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  4643322 | 7615210 | 4475581 | 4806788 |
| wifi_fade |  5335174 | 5599818 | 5550898 | 5480744 |
| wifi_suspension |  4574165 | 4616328 | 4602178 | 4573648 |
| wifi_bad_bbr |  12112441 | 11626769 | 12533043 | 11985779 |
| wifi_bad_c4 |  11690859 | 12288047 | 12435459 | 12401707 |
| wifi_bad_cubic |  11961135 | 12011172 | 13905062 | 11723366 |


## ECN

Here the statistics for the ecn test cases.

###  average time for ecn tests

|  average time for ecn tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4465878 | 4670054 | 4460773 | 4494003 |
| ecn_c4 |  12286476 | 17269928 | 13977479 | 11422019 |
| ecn_cubic |  8362141 | 9701695 | 13356991 | 8235549 |
| ecn_bbr |  13079389 | 13246715 | 16900370 | 13083701 |

###  top 90% time for ecn tests

|  top 90% time for ecn tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4466761 | 4671124 | 4457939 | 4494072 |
| ecn_c4 |  13033989 | 17698371 | 14561797 | 12383356 |
| ecn_cubic |  9108260 | 10561707 | 13961159 | 8720974 |
| ecn_bbr |  13342537 | 13372125 | 17458084 | 13345131 |


## Media

Here the statistics for the media test cases.

###  average av_latency for media tests

|  average av_latency for media tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| media |  33511 | 33427 | 33512 | 33511 |
| media10 |  45378 | 44991 | 47755 | 45204 |
| media_600fr |  33625 | 33545 | 33629 | 33624 |
| media_short_long |  100794 | 134059 | 100766 | 101036 |
| media_wb |  80894 | 85353 | 84391 | 77485 |
| media_wf |  82547 | 86474 | 83914 | 82971 |
| media_ws |  22941 | 21645 | 22495 | 22854 |
| media_ecn |  34413 | 34481 | 34716 | 34408 |

###  top 90% max_latency for media tests

|  top 90% max_latency for media tests| c4 | bbr | cubic | c4_2026_07_05 |
| --------- | ---:| ---:| ---:| ---:|
| media |  43453 | 43453 | 43453 | 43453 |
| media10 |  71128 | 71128 | 92163 | 71128 |
| media_600fr |  43453 | 43453 | 43453 | 43453 |
| media_short_long |  111153 | 334491 | 109180 | 117984 |
| media_wb |  270847 | 304794 | 274677 | 269770 |
| media_wf |  279458 | 365839 | 298720 | 298762 |
| media_ws |  197821 | 195521 | 197821 | 197821 |
| media_ecn |  47975 | 50996 | 50996 | 49700 |


# Live Tests {#live-tests}

We need real life tests as well.

## Loopback tests

Loopback tests were performed on Windows, downloading 10GB of data over
a loopback connection. They showed picoquic using C4 achieving a data rate
of 3Gbps, slightly more than the 2.9Gbps achieved when using Cubic or the
2.6 Gbps achieved when using BBR.

## Webex prototype deployments

To do. Write down.

# Security Considerations

This documentation of protocol testing does not have any
particular security considerations.

We did not include specific security oriented tests in this document.

# IANA Considerations

This document has no IANA actions.

--- back

# Acknowledgments
{:numbered="false"}

TODO acknowledge.