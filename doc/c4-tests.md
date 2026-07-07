---
title: "Testing of Christian's Congestion Control Code (C4)"
abbrev: "C4 Tests"
category: info

docname: draft-huitema-ccwg-c4-test-03
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

Here the statistics for the network events test cases.

###  average time for network events tests

|  average time for network events tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| alone |  4642195 | 4687549 | 4492758 |
| alone_200 |  1161980 | 1221731 | 1147122 |
| low_and_up |  7762235 | 7506642 | 8067973 |
| drop_and_back |  7697371 | 7627033 | 7629153 |
| blackhole |  5628028 | 5811312 | 5695731 |
| short_long |  17537092 | 42152692 | 21386022 |
| satellite |  6807111 | 7452075 | 6704244 |

###  top 90% time for network events tests

|  top 90% time for network events tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| alone |  4835141 | 4701306 | 4528876 |
| alone_200 |  1186067 | 1222109 | 1156831 |
| low_and_up |  7764215 | 7512100 | 8085544 |
| drop_and_back |  7698289 | 7631546 | 7632407 |
| blackhole |  5628156 | 5815444 | 5699325 |
| short_long |  17538424 | 43393686 | 21547041 |
| satellite |  6807137 | 7432491 | 6704247 |


## Competition

Here the statistics for the compete test cases.

###  average time for compete tests

|  average time for compete tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| vs_bbr |  2964582 | 4507849 | 2849612 |
| vs_c4 |  4490594 | 6776085 | 6902341 |
| vs_cubic |  3484869 | 6988975 | 5300570 |
| after_c4 |  5239798 | 6841587 | 7457755 |
| before_c4 |  2699206 | 4136358 | 3097226 |
| vs_c4_lg |  21067859 | 26367492 | 22958382 |
| vs_c4_lg2 |  21102894 | 21108978 | 21798180 |
| vs_bbr_lg |  16742530 | 21107935 | 15582257 |
| vs_bbr_lg2 |  20600335 | 18756082 | 21367106 |
| vs_cubic_lg |  17578391 | 21478179 | 20929801 |
| vs_cubic_lg2 |  16969990 | 15533602 | 20733296 |

###  top 90% time for compete tests

|  top 90% time for compete tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| vs_bbr |  2983881 | 4592446 | 2872270 |
| vs_c4 |  4864821 | 6841410 | 7345182 |
| vs_cubic |  3555684 | 7090854 | 5578225 |
| after_c4 |  6102901 | 7010851 | 7952653 |
| before_c4 |  3001428 | 5433864 | 3988378 |
| vs_c4_lg |  21141447 | 31989078 | 24186774 |
| vs_c4_lg2 |  21174182 | 21186594 | 22376456 |
| vs_bbr_lg |  16936214 | 21146009 | 15863189 |
| vs_bbr_lg2 |  21138531 | 19075956 | 22077739 |
| vs_cubic_lg |  18440982 | 21770804 | 21279706 |
| vs_cubic_lg2 |  17548782 | 15772770 | 20959969 |


## Wi-Fi

Here the statistics for the wifi test cases.

###  average time for wifi tests

|  average time for wifi tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| wifi_bad |  4144883 | 5518835 | 4117296 |
| wifi_fade |  5203858 | 5401158 | 5341080 |
| wifi_suspension |  4563252 | 4615927 | 4601001 |
| wifi_bad_bbr |  7581238 | 7267102 | 7604761 |
| wifi_bad_c4 |  9347050 | 9527486 | 8721036 |
| wifi_bad_cubic |  8407363 | 8851061 | 9928339 |

###  top 90% time for wifi tests

|  top 90% time for wifi tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| wifi_bad |  4806788 | 7575710 | 4437927 |
| wifi_fade |  5480744 | 5585208 | 5542744 |
| wifi_suspension |  4573648 | 4616912 | 4607139 |
| wifi_bad_bbr |  11985779 | 11799491 | 12840326 |
| wifi_bad_c4 |  12401707 | 12389220 | 13067528 |
| wifi_bad_cubic |  11723366 | 12141374 | 13952338 |


## ECN

Here the statistics for the ecn test cases.

###  average time for ecn tests

|  average time for ecn tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| ecn |  4494003 | 4669871 | 4460200 |
| ecn_c4 |  11422019 | 17079150 | 14190287 |
| ecn_cubic |  8235549 | 9963937 | 13300675 |
| ecn_bbr |  13083701 | 13239913 | 16852679 |

###  top 90% time for ecn tests

|  top 90% time for ecn tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| ecn |  4494072 | 4670724 | 4457944 |
| ecn_c4 |  12383356 | 17435154 | 14527298 |
| ecn_cubic |  8720974 | 10881018 | 13952925 |
| ecn_bbr |  13345131 | 13370326 | 17523171 |


## Media

Here the statistics for the media test cases.

###  average av_latency for media tests

|  average av_latency for media tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| media |  33511 | 33427 | 33513 |
| media10 |  45204 | 44997 | 47758 |
| media_600fr |  33624 | 33545 | 33630 |
| media_short_long |  101036 | 133981 | 100765 |
| media_wb |  77485 | 90804 | 83044 |
| media_wf |  82971 | 86612 | 83811 |
| media_ws |  22854 | 21644 | 22459 |
| media_ecn |  34408 | 34481 | 34716 |

###  top 90% max_latency for media tests

|  top 90% max_latency for media tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| media |  43453 | 43453 | 43453 |
| media10 |  71128 | 71128 | 92163 |
| media_600fr |  43453 | 43453 | 43453 |
| media_short_long |  117984 | 334491 | 110426 |
| media_wb |  269770 | 297718 | 260222 |
| media_wf |  298762 | 377437 | 313883 |
| media_ws |  197821 | 195521 | 197821 |
| media_ecn |  49700 | 50996 | 50996 |






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