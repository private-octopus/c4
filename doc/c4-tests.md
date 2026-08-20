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

# Tests and metrics

For each simulation scenario, we measure up to 4 different metrics:

* The average execution time,defined as the simulated time
  necessary to execute the scenario, which we express as two numbers:

  - the average execution time of all the simulations for the scenario,
  - and, the top 90th percentile of that time.

* The RTT observed during the connection, which we express as two numbers:

  - the average RTT value for all RTT measurements, measured for
    each simulation and averaged over all simulation.
  - and, the top 90th percentile over all tests of the sum of the average RTT
    for a connection and the standard deviation of the RTT for that
    connection.

* The load factor, defined as the share of the bandwidth used
  by the "main" connection over the period when the two connections
  compete, or, if there is only one connection, over the duration of
  the connection. We monitor:

  - the average of the load factor over all simulations for the scenario,
  - and, the top 90th percentile of that load factor.

* The frame latency, defined as the average time delay between the
  moment a media frame is scheduled and its arrival at the receiver.
  We monitor:

  - the average value of the average frame latency per connection,
  - the top 90th percentile of the max frame latency per connection.

The 4 different metrics and their variants do not make sense for
all scenarios:

  - we monitor the execution time for all scenarios except the media
    scenarios, for which the execution time is fixed by the scenario.
  
  - we monitor the RTT for all scenarios except the media
    scenarios, for which the frame latency provides better information.
  
  - for the media transmission tests, we only monitor the frame latency;
    we do not monitor the frame latency for the other test scenarios.
  
  - we only monitor the load factor for scenarios that involve multiple
    competing connections.
 
We do not try to assign fixed target values for the different scenarios.
Instead, we run three variants for each test: one in which the "main"
connection uses C4, one in which it uses BBR, and one in which it uses
Cubic. We want C4 to demonstrate that it is "better" than BBR and Cubic
for a majority of the scenarios. Depending on the scenario, we may
discuss whether better means a shorter execution time, a shorter
RTT, a load factor in the acceptable range, or a shorter frame latency.
When C4 does not provde the best results, we want to ensure that it
is not worse than both BBR and Cubic, and that the difference to the
best scenarios are reasonably small.

# Description of simulation tests  {#simulations}

We test the design by running a series of simulations, which cover:

* reaction to network events

* competition with other congestion control algorithms

* handling of buffer bloat

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

## Buffer Bloat

The buffer bloat simulations test the behavior of C4 when
the simulated path is configured with very large network buffers.
This tests the recommendation in {{RFC9743}} that algorithms
"ought to try to avoid maintaining excessive queues in the network".

All test variants use the same transmission scenario: The RTT of
the path is always 80ms.

We use 4 variants of this test: a "single connection" variant,
and 3 competition scenarios in which the background connection
uses C4, BBR or Cubic.

### Single connection with Buffer Bloat (bbloat)

The single connection with buffer bloat test simulates a
single connection trying to download 30 MB of data over a 20Mbit/s path.
The path has an 80ms RTT, and the network buffers
are configured to hold up to 20 seconds of traffic. 

The goal is to verify that C4 is about as efficient as Cubic,
while maintaining reasonably short RTTs.

### Compete with C4 over Buffer Bloat (bbloat_c4)

The compete against C4 with buffer bloat test simulates a
main connection trying to download 30 MB of data over a 20Mbit/s path,
while the background connection using C4 that starts at the same
time is trying to download 20 MB. The path has an 80ms RTT, and the network buffers
are configured to hold up to 20 seconds of traffic. 

The goal is to verify that C4 competes reasonably against itself
in the presence of buffer bloat.

### Compete with BBR over Buffer Bloat (bbloat_bbr)

The compete against C4 with buffer bloat test simulates a
main connection trying to download 30 MB of data over a 20Mbit/s path,
while the background connection using BBR that starts at the same
time is trying to download 20 MB. The path has an 80ms RTT, and the network buffers
are configured to hold up to 20 seconds of traffic. 

The goal is to verify that C4 competes reasonably against BBR
in the presence of buffer bloat.

### Compete with Cubic over Buffer Bloat (bbloat_cubic)

The compete against C4 with buffer bloat test simulates a
main connection trying to download 30 MB of data over a 20Mbit/s path,
while the background connection using Cubic that starts at the same
time is trying to download 20 MB. The path has an 80ms RTT, and the network buffers
are configured to hold up to 20 seconds of traffic. 

We already know that the Cubic algorithm only backs off in response
to packet losses, and thus will essentially never back off during
our tests. In this test, we want to verify that
C4 is not "shut off" when competing with Cubic, even if that means
accepting larger RTTs.


## Competition

In accordance with {{RFC9743}}, we evaluate competition between
C4 connections, or between C4 and Cubic or BBR. We design a series of tests,
each correponding to a competition scenario between a "main" connection and
a "background" connection. For each test, we run the test using either C4,
Cubic or BBR for the "main" connection. The test scenario specifies the
algorithm managing the background connection, as well as scenario details.

We test that the bandwidth is shared reasonably by monitoring the
"load" of the network, defined as the share of the bandwidth used
by the "main" connection over the period when the two connections
compete.

According to {{RFC9743}}, a proposed congestion control algorithm
such as C4 shall avoid having a significantly negative
impact on flows using a standard congestion control. Impact here
encompasses causing packet losses or long queues, or simply consuming
too much of the available bandwidth. Using less than 50% of the capacity would
be considered good, using more than 70% would be considered bad, and more
than 80% really bad. This has a negative side too: using less
than 30% of the bandwidth means that algorithm is too during
competing period, and using less than 20% would be really bad.

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
# Statistics
Here is a collection of statistics on all test cases.

## Reaction to network events

Here the statistics for the network events test cases.

###  average time for network events tests

|  average time for network events tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4537379 | 4688667 | 4478760 | 4502535 |
| alone_200 |  1134449 | 1221419 | 1146301 | 1120932 |
| alone_1_5M |  21913393 | 21711918 | 21518869 | 21505342 |
| alone_512k |  16411058 | 16211845 | 16182299 | 16173448 |
| low_and_up |  7671447 | 7506560 | 8032634 | 7568928 |
| drop_and_back |  7576003 | 7627284 | 7629726 | 7549810 |
| blackhole |  5631908 | 5810827 | 5695585 | 5591990 |
| short_long |  17994083 | 42398752 | 21383946 | 17536760 |
| satellite |  7093448 | 7452095 | 6704244 | 6807132 |

###  top 90% time for network events tests

|  top 90% time for network events tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4747563 | 4699327 | 4532722 | 4561489 |
| alone_200 |  1194997 | 1222000 | 1151517 | 1186748 |
| alone_1_5M |  21913422 | 21718517 | 21552443 | 21511156 |
| alone_512k |  16417540 | 16217227 | 16193306 | 16173943 |
| low_and_up |  7672526 | 7511571 | 8071887 | 7570219 |
| drop_and_back |  7642736 | 7631362 | 7632416 | 7569623 |
| blackhole |  5631796 | 5814609 | 5699325 | 5592062 |
| short_long |  18003455 | 43395010 | 21553745 | 17538427 |
| satellite |  7094728 | 7432285 | 6704247 | 6807184 |

###  average RTT for network events tests

|  average RTT for network events tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  103560 | 95763 | 129990 | 113628 |
| alone_200 |  58885 | 45827 | 53624 | 59296 |
| alone_1_5M |  53716 | 55261 | 86189 | 92468 |
| alone_512k |  67450 | 94641 | 95106 | 98346 |
| low_and_up |  111353 | 116923 | 118924 | 109493 |
| drop_and_back |  120481 | 120573 | 129505 | 119770 |
| blackhole |  127697 | 146368 | 168224 | 140658 |
| short_long |  192964 | 193578 | 305164 | 193251 |
| satellite |  601410 | 692052 | 610161 | 601057 |

###  top 90% of RTT + standard deviation for network events tests

|  top 90% of RTT + standard deviation for network events tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  127710 | 108024 | 167582 | 142636 |
| alone_200 |  86744 | 57164 | 70940 | 88089 |
| alone_1_5M |  65744 | 64697 | 90880 | 96658 |
| alone_512k |  85285 | 104064 | 106122 | 105400 |
| low_and_up |  130074 | 134293 | 147287 | 127962 |
| drop_and_back |  147704 | 150259 | 161338 | 153647 |
| blackhole |  434408 | 502910 | 485331 | 479784 |
| short_long |  232410 | 232481 | 409879 | 230002 |
| satellite |  602640 | 830708 | 621272 | 602375 |


## Competition

Here the statistics for the compete test cases.

###  average time for compete tests

|  average time for compete tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2856946 | 4510488 | 2854062 | 2817345 |
| vs_c4 |  4310580 | 6849206 | 6559596 | 4349648 |
| vs_cubic |  4149134 | 6974622 | 5369346 | 3418203 |
| after_c4 |  6681165 | 6879464 | 7210930 | 6599569 |
| before_c4 |  2682844 | 3391494 | 2370418 | 2633286 |
| vs_c4_lg |  21079063 | 22737360 | 22134217 | 21024837 |
| vs_c4_lg2 |  21026474 | 21036378 | 21425730 | 20961838 |
| vs_bbr_lg |  19611040 | 21101368 | 15578426 | 15608340 |
| vs_bbr_lg2 |  17099417 | 18735829 | 21345356 | 16501414 |
| vs_cubic_lg |  20768151 | 21422001 | 20943454 | 17942252 |
| vs_cubic_lg2 |  18417916 | 15526244 | 20645605 | 17076722 |

###  top 90% time for compete tests

|  top 90% time for compete tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2875599 | 4587718 | 2873382 | 2821436 |
| vs_c4 |  4594237 | 6864615 | 6996747 | 4521899 |
| vs_cubic |  4433582 | 7089364 | 5580488 | 3787691 |
| after_c4 |  6800520 | 6992550 | 7340745 | 6743285 |
| before_c4 |  2781418 | 3450285 | 2594098 | 2734795 |
| vs_c4_lg |  21200617 | 27655734 | 22801481 | 21132908 |
| vs_c4_lg2 |  21109107 | 21102400 | 21822041 | 21040360 |
| vs_bbr_lg |  20389440 | 21136912 | 15869550 | 15857330 |
| vs_bbr_lg2 |  17400154 | 19001808 | 21902726 | 16599500 |
| vs_cubic_lg |  21060949 | 21759397 | 21356186 | 20324018 |
| vs_cubic_lg2 |  19085838 | 15726319 | 20925202 | 17505908 |

###  average RTT for compete tests

|  average RTT for compete tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  101969 | 125718 | 132041 | 104812 |
| vs_c4 |  113530 | 106788 | 132536 | 114721 |
| vs_cubic |  130957 | 117933 | 139724 | 136494 |
| after_c4 |  122354 | 110392 | 98618 | 115545 |
| before_c4 |  74206 | 70131 | 74876 | 76660 |
| vs_c4_lg |  104283 | 96524 | 118789 | 113389 |
| vs_c4_lg2 |  119714 | 128421 | 112152 | 124713 |
| vs_bbr_lg |  130053 | 127812 | 145698 | 117425 |
| vs_bbr_lg2 |  110452 | 136417 | 115941 | 121149 |
| vs_cubic_lg |  125771 | 94416 | 129414 | 135951 |
| vs_cubic_lg2 |  122158 | 131659 | 136856 | 126129 |

###  top 90% of RTT + standard deviation for compete tests

|  top 90% of RTT + standard deviation for compete tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  121593 | 156837 | 163464 | 128620 |
| vs_c4 |  148547 | 131959 | 154752 | 146435 |
| vs_cubic |  154934 | 151887 | 163780 | 161246 |
| after_c4 |  147956 | 138768 | 120679 | 146628 |
| before_c4 |  105447 | 95288 | 105477 | 108898 |
| vs_c4_lg |  137883 | 128469 | 152037 | 146006 |
| vs_c4_lg2 |  151314 | 157237 | 141373 | 158239 |
| vs_bbr_lg |  157618 | 160547 | 163004 | 142672 |
| vs_bbr_lg2 |  134788 | 155570 | 151418 | 147234 |
| vs_cubic_lg |  148792 | 120098 | 162781 | 151416 |
| vs_cubic_lg2 |  143582 | 149662 | 159670 | 146915 |

###  average load for compete tests

|  average load for compete tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  78% | 47% | 78% | 78% |
| vs_c4 |  51% | 24% | 35% | 51% |
| vs_cubic |  51% | 18% | 41% | 65% |
| after_c4 |  34% | 20% | 26% | 35% |
| before_c4 |  60% | 48% | 69% | 61% |
| vs_c4_lg |  51% | 29% | 36% | 52% |
| vs_c4_lg2 |  55% | 56% | 40% | 55% |
| vs_bbr_lg |  64% | 50% | 81% | 80% |
| vs_bbr_lg2 |  72% | 67% | 41% | 75% |
| vs_cubic_lg |  55% | 18% | 48% | 70% |
| vs_cubic_lg2 |  67% | 81% | 57% | 72% |

###  top 90% load for compete tests

|  top 90% load for compete tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  79% | 47% | 79% | 78% |
| vs_c4 |  53% | 25% | 36% | 52% |
| vs_cubic |  58% | 18% | 46% | 70% |
| after_c4 |  37% | 20% | 27% | 36% |
| before_c4 |  61% | 49% | 74% | 65% |
| vs_c4_lg |  55% | 32% | 38% | 54% |
| vs_c4_lg2 |  58% | 58% | 43% | 58% |
| vs_bbr_lg |  66% | 51% | 81% | 81% |
| vs_bbr_lg2 |  74% | 69% | 48% | 76% |
| vs_cubic_lg |  61% | 18% | 51% | 78% |
| vs_cubic_lg2 |  68% | 82% | 61% | 74% |


## Buffer bloat

Here the statistics for the buffer bloat test cases.

###  average time for buffer bloat tests

|  average time for buffer bloat tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  12977227 | 13007888 | 12626182 | 12642775 |
| bbloat_c4 |  20999196 | 22827475 | 20715668 | 20935286 |
| bbloat_bbr |  15731469 | 21353800 | 14419567 | 15320599 |
| bbloat_cubic |  20944222 | 21609223 | 20716693 | 20736042 |

###  top 90% time for buffer bloat tests

|  top 90% time for buffer bloat tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  12977849 | 13007933 | 12626286 | 12645052 |
| bbloat_c4 |  21099096 | 24185358 | 20715704 | 21006655 |
| bbloat_bbr |  16235602 | 21448030 | 14535755 | 15431313 |
| bbloat_cubic |  20970748 | 21866067 | 20717185 | 20761314 |

###  average RTT for buffer bloat tests

|  average RTT for buffer bloat tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  99261 | 84749 | 229431 | 113442 |
| bbloat_c4 |  161688 | 93877 | 660080 | 232427 |
| bbloat_bbr |  125542 | 124316 | 265981 | 132447 |
| bbloat_cubic |  567322 | 97461 | 647456 | 551412 |

###  top 90% of RTT + standard deviation for buffer bloat tests

|  top 90% of RTT + standard deviation for buffer bloat tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  150826 | 95924 | 322465 | 158066 |
| bbloat_c4 |  244849 | 127703 | 941731 | 361518 |
| bbloat_bbr |  174746 | 159721 | 408582 | 160200 |
| bbloat_cubic |  901520 | 136800 | 1039501 | 860075 |

###  average load for buffer bloat tests

|  average load for buffer bloat tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  95% | 95% | 98% | 98% |
| bbloat_c4 |  51% | 20% | 35% | 50% |
| bbloat_bbr |  79% | 48% | 86% | 81% |
| bbloat_cubic |  59% | 15% | 53% | 58% |

###  top 90% load for buffer bloat tests

|  top 90% load for buffer bloat tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  95% | 95% | 98% | 98% |
| bbloat_c4 |  54% | 21% | 36% | 53% |
| bbloat_bbr |  80% | 49% | 86% | 82% |
| bbloat_cubic |  59% | 15% | 54% | 58% |


## Wi-Fi

Here the statistics for the wifi test cases.

###  average time for wifi tests

|  average time for wifi tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  3822742 | 5563466 | 4085282 | 4070606 |
| wifi_fade |  5066217 | 5415997 | 5363083 | 5073239 |
| wifi_suspension |  4549622 | 4615846 | 4600921 | 4564955 |
| wifi_bad_bbr |  7718529 | 7111352 | 7147651 | 7065591 |
| wifi_bad_c4 |  9458304 | 10210890 | 9182123 | 8664120 |
| wifi_bad_cubic |  9570969 | 9190420 | 10563955 | 8649554 |

###  top 90% time for wifi tests

|  top 90% time for wifi tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  4346110 | 7528620 | 4485517 | 4530282 |
| wifi_fade |  5307866 | 5563101 | 5546986 | 5357776 |
| wifi_suspension |  4548819 | 4616391 | 4602118 | 4574186 |
| wifi_bad_bbr |  11940414 | 11352741 | 12874917 | 10736818 |
| wifi_bad_c4 |  11692622 | 12161764 | 12022842 | 11249655 |
| wifi_bad_cubic |  12123720 | 12170980 | 13796271 | 11250821 |

###  average RTT for wifi tests

|  average RTT for wifi tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  121371 | 65736 | 93562 | 109023 |
| wifi_fade |  142313 | 125809 | 136817 | 145297 |
| wifi_suspension |  13383 | 13462 | 26530 | 13993 |
| wifi_bad_bbr |  200267 | 213255 | 221586 | 199724 |
| wifi_bad_c4 |  218392 | 229337 | 232956 | 234733 |
| wifi_bad_cubic |  234227 | 249556 | 199344 | 256603 |

###  top 90% of RTT + standard deviation for wifi tests

|  top 90% of RTT + standard deviation for wifi tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  255630 | 145718 | 148114 | 258484 |
| wifi_fade |  204235 | 176378 | 183083 | 207083 |
| wifi_suspension |  28966 | 34221 | 45311 | 32878 |
| wifi_bad_bbr |  333610 | 350763 | 330507 | 330282 |
| wifi_bad_c4 |  313839 | 337064 | 328943 | 331655 |
| wifi_bad_cubic |  326588 | 359388 | 323112 | 337028 |

###  average load for wifi tests

|  average load for wifi tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  94% | 66% | 87% | 84% |
| wifi_fade |  87% | 78% | 80% | 87% |
| wifi_suspension |  91% | 89% | 91% | 91% |
| wifi_bad_bbr |  60% | 73% | 75% | 64% |
| wifi_bad_c4 |  46% | 46% | 51% | 51% |
| wifi_bad_cubic |  43% | 67% | 38% | 50% |

###  top 90% load for wifi tests

|  top 90% load for wifi tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  106% | 95% | 96% | 95% |
| wifi_fade |  89% | 80% | 85% | 90% |
| wifi_suspension |  91% | 89% | 91% | 91% |
| wifi_bad_bbr |  83% | 120% | 102% | 82% |
| wifi_bad_c4 |  59% | 68% | 72% | 69% |
| wifi_bad_cubic |  62% | 137% | 74% | 67% |


## ECN

Here the statistics for the ecn test cases.

###  average time for ecn tests

|  average time for ecn tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4550676 | 4669890 | 4461982 | 4465982 |
| ecn_c4 |  12102397 | 16439629 | 14000412 | 12078995 |
| ecn_cubic |  10002899 | 9936183 | 13395586 | 8246796 |
| ecn_bbr |  13260748 | 13254102 | 16978634 | 13089999 |

###  top 90% time for ecn tests

|  top 90% time for ecn tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4551316 | 4670725 | 4457947 | 4467028 |
| ecn_c4 |  12782540 | 16813350 | 14167609 | 13032878 |
| ecn_cubic |  10883690 | 10599006 | 13967821 | 9023342 |
| ecn_bbr |  13425165 | 13376811 | 17530934 | 13309789 |

###  average RTT for ecn tests

|  average RTT for ecn tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  84132 | 89461 | 121761 | 84661 |
| ecn_c4 |  108327 | 95400 | 105768 | 106047 |
| ecn_cubic |  114493 | 124067 | 114706 | 116735 |
| ecn_bbr |  108695 | 101580 | 94816 | 114200 |

###  top 90% of RTT + standard deviation for ecn tests

|  top 90% of RTT + standard deviation for ecn tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  95241 | 101703 | 139685 | 96364 |
| ecn_c4 |  130209 | 111530 | 125448 | 129210 |
| ecn_cubic |  127673 | 135620 | 137793 | 134465 |
| ecn_bbr |  133716 | 123290 | 114279 | 136495 |


## Media

Here the statistics for the media test cases.

###  average av_latency for media tests

|  average av_latency for media tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| media |  33510 | 33427 | 33512 | 33512 |
| media10 |  45672 | 45002 | 47758 | 45369 |
| media_600fr |  33620 | 33545 | 33629 | 33625 |
| media_short_long |  100812 | 133985 | 100763 | 100792 |
| media_wb |  84810 | 89231 | 82204 | 80315 |
| media_wf |  80990 | 87161 | 84599 | 82068 |
| media_ws |  22980 | 21644 | 22139 | 22955 |
| media_ecn |  34487 | 34481 | 34716 | 34413 |

###  top 90% max_latency for media tests

|  top 90% max_latency for media tests| c4 | bbr | cubic | c4-draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| media |  43453 | 43453 | 43453 | 43453 |
| media10 |  87527 | 71128 | 92163 | 71128 |
| media_600fr |  43453 | 43453 | 43453 | 43453 |
| media_short_long |  111799 | 334491 | 109180 | 111153 |
| media_wb |  317480 | 307211 | 262539 | 265337 |
| media_wf |  290759 | 345716 | 322625 | 290560 |
| media_ws |  197821 | 195521 | 197821 | 197821 |
| media_ecn |  50966 | 50996 | 50996 | 47975 |


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