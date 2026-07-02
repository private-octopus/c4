---
title: "Testing of Christian's Congestion Control Code (C4)"
abbrev: "C4 Tests"
category: info

docname: draft-huitema-ccwg-c4-test-latest
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
   I-D.ietf-tsvwg-careful-resume:
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

### Simulation of a simple 20Mbps connection

This scenario simulates a 10MB download over a 20 Mbps link,
with an 80ms RTT, and a bottlneck buffer capacity corresponding
to 1 BDP.

In a typical simulation, we see a initial phase complete in less
than 800ms, followed by a recovery phase in which the
transmission rate stabilizes to the line rate. After that,
the RTT remains very close to the path RTT, except for
periodic small bumps during the "push" transitions.

### Simulation of a simple 200Mbps connection

This scenario simulates a 20MB download over a 200 Mbps link,
with a 40ms RTT, and a bottleneck buffer capacity corresponding
to 1 BDP.

This short test shows that the initial phase correctly discover
the path capacity, and that the transmission operates at
the expected rate after that.

### Simulation of a geostationary satellite connection

This scenario simulates a 100MB download over a 250 Mbps link,
with a 600ms RTT, and a bottleneck buffer capacity corresponding
to 1 BDP, i.e., simulating a geostationary satellite connection.
The scenario also tests the support for careful resume
{{I-D.ietf-tsvwg-careful-resume}} by setting
the remembered CWND to 18750000 bytes and the
remembered RTT to 600.123ms.

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


## L4S and ECN {#ecn-simulations}

The "ECN" test simulates a 20 Mbps link,
with an 80ms RTT, and a bottleneck buffer capacity corresponding
to 1 BDP. 

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

## Competition with itself

In accordance with {{RFC9743}}, we design series of tests
of multiple competing flows all using C4. We want to test
different conditions, such as data rate and latency,
and also different scenarios, such as testing whether
the "background" connection starts at the same time, before
or after the "main" connection.

We test that the bandwidth is shared reasonably by testing
the completion time of a download, and setting the target
value so it can only be achieved if the main connection
gets "about half" of the bandwidth.

### Two short C4 simultaneous connections

Our first test simulates two C4 connections starting at the
same time and using the same path. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 10MB, the main connection downloads 5MB.

### Short background C4 connection first

The "background first" test simulates two C4 connections using the same path
with the background connection starting
0.5 seconds before the main connection. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 10MB, the main connection downloads 5MB.

### Short background C4 connection last

The "background last"  simulates two C4 connections using the same path
with the background connection starting at the
0.5 seconds after the main connection. The path has a 50Mbps data rate
and 30ms RTT. The background connection
tries to download 20MB, the main connection downloads 10MB.

### Two long C4 connections

The long connection test simulates two C4 connections starting at the
same time and using the same path. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 30MB, the main connection downloads 20MB.

### Long background C4 connection last

The long "background last" test simulates two C4 connections using the same path
with the background connection starting
1 second after the main connection. The path has a 10Mbps data rate
and 70ms RTT. The background connection
tries to download 15MB, the main connection downloads 10MB.

### Compete with C4 over bad Wi-Fi

The "compete over bad Wi-Fi" test simulates two C4 connections using 
the same "bad Wi-Fi" path and starting, with the main
connection starting 1 second after the background connection.
The path has a 10Mbps data rate and 2ms RTT, plus Wi-Fi jitter
set to 7ms average -- 
the same jitter characteristics as in the "bad Wi-Fi" test (see {{bad-wifi}}).
The background connection
tries to download 10MB, the main connection downloads 4MB.

## Competition with Cubic

In accordance with {{RFC9743}}, we design series of tests
of multiple competing flows using C4 and Cubic. We want to test
different conditions, such as data rate and latency,
and also different scenarios, such as testing whether
the "background" connection starts at the same time, before
or after the "main" connection.

We test that the bandwidth is shared reasonably by testing
the completion time of a download, and setting the target
value so it can only be achieved if the main connection
gets "about half" of the bandwidth.

### Two short C4 and Cubic connections

Our first test simulates two C4 and Cubic connections starting at the
same time and using the same path. The path has a 20Mbps data rate
and 80ms RTT. The background Cubic connection
tries to download 10MB, the main connection downloads 5MB.

### Two long C4 and Cubic connections

The long connection test simulates two C4 and Cubic connections starting at the
same time and using the same path. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 30MB, the main connection downloads 20MB.

### Long Cubic background connection last

The long "background last" test simulates two C4 and Cubic connections
using the same path
with the background Cubic connection starting
1 second after the main connection. The path has a 10Mbps data rate
and 70ms RTT. The background connection
tries to download 15MB, the main connection downloads 10MB.

### Compete with Cubic over bad Wi-Fi

The "compete over bad Wi-Fi" test simulates two C4 and Cubic connections using 
the same "bad Wi-Fi" path, with the main
connection starting 1 second after the background connection.
The path has a 10Mbps data rate and 2ms RTT, plus Wi-Fi jitter
set to 7ms average -- 
the same jitter characteristics as in the "bad Wi-Fi" test (see {{bad-wifi}}).
The background connection
tries to download 10MB, the main connection downloads 4MB.

## Competition with BBR

In accordance with {{RFC9743}}, we design series of tests
of multiple competing flows using C4 and BBR. We want to test
different conditions, such as data rate and latency,
and also different scenarios, such as testing whether
the "background" connection starts at the same time, before
or after the "main" connection.

We test that the bandwidth is shared reasonably by testing
the completion time of a download, and setting the target
value so it can only be achieved if the main connection
gets "about half" of the bandwidth.

### Two short C4 and BBR connections

Our first test simulates two C4 and BBR connections starting at the
same time and using the same path. The path has a 20Mbps data rate
and 80ms RTT. The background BBR connection
tries to download 10MB, the main connection downloads 5MB.

### Two long C4 and BBR connections

The long connection test simulates two C4 and BBR connections starting at the
same time and using the same path. The path has a 20Mbps data rate
and 80ms RTT. The background connection
tries to download 30MB, the main connection downloads 20MB.

### Long BBR background connection last

The long "background last" test simulates two C4 and BBR connections
using the same path
with the background BBR connection starting
1 second after the main connection. The path has a 10Mbps data rate
and 70ms RTT. The background connection
tries to download 15MB, the main connection downloads 10MB.

### Compete with BBR over bad Wi-Fi

The "compete over bad Wi-Fi" test simulates two C4 and BBR connections using 
the same "bad Wi-Fi" path, with the main
connection starting 1 second after the background BBR connection.
The path has a 10Mbps data rate and 2ms RTT, plus Wi-Fi jitter
set to 7ms average -- 
the same jitter characteristics as in the "bad Wi-Fi" test (see {{bad-wifi}}).
The background connection
tries to download 10MB, the main connection downloads 4MB.

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

### Media on High Speed Connection

The "high speed" media test verifies how C4 handles media on a 100 Mbps
connection with a 30ms RTT. The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection. The expected average delay is set to 31ms,
and the maximum delay is set to 79ms. The test is successful if
100 trials are all successful.

### Media on 10 Mbps Connection

The "high speed" media test verifies how C4 handles media on a 10 Mbps
connection with a 40ms RTT.  The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection. The expected average delay is set to 47ms,
and the maximum delay is set to 160ms. The test is successful if
100 trials are all successful.

### Media for 20 seconds

The "20 seconds" media test verifies that media performance does not
degrade over time, simulating a 100Mbps connection with a 30ms RTT.
The test lasts for 20 video groups of frames, i.e. 20 seconds. 
The measurements start 200ms after the
start of the connection.

### Media over varying RTT

The "varying RTT" media test verifies that media performance does not
degrade over time, simulating a 100Mbps connection with a 30ms RTT,
that changes to a 100ms RTT after 1 second.
The test lasts for 10 video groups of frames, i.e. 10 seconds. 
The measurements start 5 seconds after the
start of the connection.

### Media over varying Wi-Fi

The "varying Wi-Fi" media test verifies that media performance does not
degrade too much on a connection that has the kind of jitter
discussed in {{c4-wifi}}. The connection has the characteristics
similar to the "fading Wi-Fi" scenario described in {{wifi-fade}}.
The connection starts
with a data rate of 20Mbps, 40ms RTT, and Wi-Fi jitter
with average 1ms. After 1 second, the data rate drops to 2Mbps
and the jitter average increases to 12ms.
The test lasts for 5 video groups of frames,
i.e. 5 seconds. The measurements start 200ms after the
start of the connection.

### Media with Wi-Fi suspensions

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

### Media over bad Wi-Fi

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

# Simulation results {#results}

Simulations include random events, such as network jitter or the
precise timing of packet arrivals and departure. Minute changes in starting
conditions can have cascading effects. To get reliable results, we run each test 100
times. The simulator produces a log of each test execution (in QLOG formt), and a summary
of each test results, including the completion time for each test, and for tests
checking media the average and max frame delivery time.

We present here a summary of the results, including the average and the 90th percentile
of the completion time for each test. For media tests, we also report the average frame
delivery time and the 90th percentile of the max frame delivery time.

We run these tests for C4, Cubic and BBR, and present the results for these 3
congestion control algorithms in a set of tables. All times are expressed in microseconds,
and for all results lower time values are considered better.


## Reaction to network events

Here the statistics for the reaction to network events test cases.

### average time for network events tests
|  average time for network events tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| alone |  4653960 | 4692310 | 4494405 |
| alone_200 |  1162182 | 1221435 | 1145092 |
| low_and_up |  7760545 | 7506752 | 8059510 |
| drop_and_back |  7696412 | 7626970 | 7629698 |
| blackhole |  5628021 | 5811682 | 5696055 |
| short_long |  17536604 | 42329502 | 21377188 |
| satellite |  6807146 | 7470544 | 6704246 |
###  top 90% time for network events tests
|  top 90% time for network events tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| alone |  4858618 | 4698069 | 4536655 |
| alone_200 |  1185228 | 1221964 | 1148340 |
| low_and_up |  7763802 | 7511592 | 8073032 |
| drop_and_back |  7698264 | 7630837 | 7632446 |
| blackhole |  5628148 | 5815443 | 5699325 |
| short_long |  17538410 | 43393690 | 21542453 |
| satellite |  6807183 | 7468719 | 6704247 |

## Competition
Here the statistics for the competition test cases.

###  average time for compete tests
|  average time for compete tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| vs_bbr |  2971282 | 4510520 | 2855400 |
| vs_c4 |  4443842 | 6750580 | 6834954 |
| vs_cubic |  3479269 | 6977038 | 5370859 |
| after_c4 |  5259943 | 6828214 | 7484218 |
| before_c4 |  2718001 | 4029544 | 3088462 |
| vs_c4_lg |  21047335 | 26137478 | 23243282 |
| vs_c4_lg2 |  21109261 | 21112506 | 21776564 |
| vs_bbr_lg |  16667356 | 21100357 | 15580530 |
| vs_bbr_lg2 |  20738935 | 18712904 | 21451415 |
| vs_cubic_lg |  17440089 | 21424354 | 20956933 |
| vs_cubic_lg2 |  16951492 | 15503250 | 20662183 |
###  top 90% time for compete tests
|  top 90% time for compete tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| vs_bbr |  3002911 | 4577276 | 2874349 |
| vs_c4 |  4684679 | 6824522 | 7360124 |
| vs_cubic |  3555684 | 7090235 | 5582943 |
| after_c4 |  6241866 | 7010853 | 7991923 |
| before_c4 |  3056297 | 5435590 | 3992400 |
| vs_c4_lg |  21141990 | 31955007 | 24370656 |
| vs_c4_lg2 |  21175546 | 21189102 | 22317971 |
| vs_bbr_lg |  16926578 | 21134232 | 15835426 |
| vs_bbr_lg2 |  21125373 | 18970096 | 22033326 |
| vs_cubic_lg |  18267834 | 21762622 | 21270070 |
| vs_cubic_lg2 |  17409475 | 15667556 | 21010167 |

## Wi-Fi
Here the statistics for the Wi-Fi test cases.

###  average time for wifi tests
|  average time for wifi tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| wifi_bad |  4168880 | 5358894 | 4088419 |
| wifi_fade |  5166804 | 5416554 | 5375971 |
| wifi_suspension |  4565884 | 4615838 | 4600863 |
| wifi_bad_bbr |  7093864 | 7528061 | 7505306 |
| wifi_bad_c4 |  8659345 | 9640362 | 8677007 |
| wifi_bad_cubic |  8384000 | 9339505 | 10093469 |
###  top 90% time for wifi tests
|  top 90% time for wifi tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| wifi_bad |  4695229 | 7533280 | 4368938 |
| wifi_fade |  5446254 | 5586999 | 5588862 |
| wifi_suspension |  4574263 | 4616324 | 4601013 |
| wifi_bad_bbr |  10509521 | 12166234 | 13474180 |
| wifi_bad_c4 |  11819738 | 12510546 | 12362274 |
| wifi_bad_cubic |  10683265 | 12402231 | 13827776 |

## ECN
Here the statistics for the ECN test cases.

###  average time for ecn tests
|  average time for ecn tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| ecn |  4494164 | 4670054 | 4459971 |
| ecn_c4 |  11457544 | 16972228 | 14090566 |
| ecn_cubic |  8138028 | 9887030 | 13377954 |
| ecn_bbr |  13106071 | 13322582 | 16989106 |
###  top 90% time for ecn tests
|  top 90% time for ecn tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| ecn |  4494072 | 4670724 | 4457946 |
| ecn_c4 |  12383231 | 17366426 | 14527292 |
| ecn_cubic |  8575740 | 10507068 | 13951250 |
| ecn_bbr |  13360959 | 13370386 | 17545952 |

## Media
Here the statistics for the media test cases.

###  average av_latency for media tests
|  average av_latency for media tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| media |  33511 | 33427 | 33512 |
| media10 |  45207 | 44995 | 47756 |
| media_600fr |  33625 | 33545 | 33629 |
| media_short_long |  101035 | 133945 | 100760 |
| media_wb |  80282 | 89858 | 78149 |
| media_wf |  82690 | 87815 | 83163 |
| media_ws |  22862 | 21644 | 22482 |
| media_ecn |  34406 | 34481 | 34716 |
###  top 90% max_latency for media tests
|  top 90% max_latency for media tests| c4 | bbr | cubic |
| --------- | ---:| ---:| ---:|
| media |  43453 | 43453 | 43453 |
| media10 |  71128 | 71128 | 92163 |
| media_600fr |  43453 | 43453 | 43453 |
| media_short_long |  117838 | 334491 | 109180 |
| media_wb |  267162 | 313005 | 270403 |
| media_wf |  296181 | 408626 | 314052 |
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