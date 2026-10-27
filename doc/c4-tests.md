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

### Media on High Speed Connection (media) {#media_tests}

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

### Media with a congested feedback path (media_backload)

The "media backload" test verifies the performance of media transmission
when the feedback path is congested. The test uses the same network
characteristics as the "media" test (see {{media_tests}}), but adds
traffic on the feedback path, simulating a Cubic connection downloading
10 MB of data, starting 2 seconds into the test. Using Cubic, the
congestion window of this connection will grows steadily, building a queue
and increasing the RTT values measured by the media connection.

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

## Reaction to network events

Here the statistics for the network events test cases.

###  average time for network events tests

|  average time for network events tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4637997 | 4689121 | 4476526 | 4502535 |
| alone_200 |  1145841 | 1218599 | 1115876 | 1120932 |
| alone_1_5M |  21681915 | 21507264 | 21513019 | 21505342 |
| alone_512k |  16283457 | 16176884 | 16224485 | 16173448 |
| low_and_up |  8034286 | 7511805 | 8069761 | 7568928 |
| drop_and_back |  7913757 | 7649319 | 7581882 | 7549810 |
| blackhole |  5672040 | 5803503 | 5650972 | 5591990 |
| short_long |  17796792 | 42080755 | 21453915 | 17536760 |
| satellite |  7154694 | 7170091 | 6704245 | 6807132 |

###  top 90% time for network events tests

|  top 90% time for network events tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4906091 | 4724609 | 4502395 | 4561489 |
| alone_200 |  1193595 | 1218673 | 1116173 | 1186748 |
| alone_1_5M |  21682246 | 21510873 | 21537688 | 21511156 |
| alone_512k |  16285709 | 16191787 | 16374860 | 16173943 |
| low_and_up |  8040922 | 7517150 | 8084588 | 7570219 |
| drop_and_back |  7924271 | 7670620 | 7596371 | 7569623 |
| blackhole |  5672078 | 5805059 | 5654402 | 5592062 |
| short_long |  17798900 | 44361391 | 21547035 | 17538427 |
| satellite |  7157099 | 7170308 | 6704246 | 6807184 |

###  average RTT for network events tests

|  average RTT for network events tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  94052 | 95441 | 127233 | 113628 |
| alone_200 |  56947 | 46022 | 54918 | 59296 |
| alone_1_5M |  50668 | 64430 | 86178 | 92468 |
| alone_512k |  62632 | 95935 | 95271 | 98346 |
| low_and_up |  109615 | 116948 | 118071 | 109493 |
| drop_and_back |  115077 | 120227 | 130108 | 119770 |
| blackhole |  135412 | 148336 | 149068 | 140658 |
| short_long |  195044 | 193616 | 300239 | 193251 |
| satellite |  601388 | 706266 | 610161 | 601057 |

###  top 90% of RTT + standard deviation for network events tests

|  top 90% of RTT + standard deviation for network events tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  112001 | 107461 | 168048 | 142636 |
| alone_200 |  83970 | 57616 | 72160 | 88089 |
| alone_1_5M |  59744 | 75586 | 90860 | 96658 |
| alone_512k |  73775 | 103452 | 106089 | 105400 |
| low_and_up |  128064 | 134441 | 145844 | 127962 |
| drop_and_back |  139505 | 150495 | 161227 | 153647 |
| blackhole |  460294 | 502570 | 451075 | 479784 |
| short_long |  234671 | 232694 | 410060 | 230002 |
| satellite |  602614 | 845436 | 621273 | 602375 |


## Competition

Here the statistics for the compete test cases.

###  average time for compete tests

|  average time for compete tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2768730 | 4480386 | 2710457 | 2817345 |
| vs_c4 |  4961834 | 6307900 | 4307723 | 4349648 |
| vs_cubic |  4067022 | 6999001 | 5846835 | 3418203 |
| after_c4 |  6216225 | 6907758 | 6961798 | 6599569 |
| before_c4 |  2699634 | 3063520 | 2488607 | 2633286 |
| vs_c4_lg |  21411570 | 21131629 | 23547764 | 21024837 |
| vs_c4_lg2 |  21184515 | 21153080 | 21638712 | 20961838 |
| vs_bbr_lg |  17050469 | 21348475 | 15267288 | 15608340 |
| vs_bbr_lg2 |  17548233 | 20431875 | 22158986 | 16501414 |
| vs_cubic_lg |  18665679 | 21218334 | 21175277 | 17942252 |
| vs_cubic_lg2 |  16228274 | 15856559 | 20942576 | 17076722 |

###  top 90% time for compete tests

|  top 90% time for compete tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2777054 | 4561853 | 2715758 | 2821436 |
| vs_c4 |  5636359 | 6905450 | 4554841 | 4521899 |
| vs_cubic |  4494984 | 7028329 | 6129052 | 3787691 |
| after_c4 |  6366914 | 6980237 | 7190470 | 6743285 |
| before_c4 |  2844430 | 3213300 | 2513886 | 2734795 |
| vs_c4_lg |  21584135 | 21354591 | 24412209 | 21132908 |
| vs_c4_lg2 |  21244879 | 21310222 | 21894728 | 21040360 |
| vs_bbr_lg |  20844030 | 21449486 | 14879493 | 15857330 |
| vs_bbr_lg2 |  18244846 | 21336509 | 22520144 | 16599500 |
| vs_cubic_lg |  19640302 | 21412469 | 21953566 | 20324018 |
| vs_cubic_lg2 |  17724897 | 16019731 | 21125242 | 17505908 |

###  average RTT for compete tests

|  average RTT for compete tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  124492 | 125857 | 123340 | 104812 |
| vs_c4 |  113430 | 124494 | 132850 | 114721 |
| vs_cubic |  129619 | 109457 | 139598 | 136494 |
| after_c4 |  116592 | 108393 | 112248 | 115545 |
| before_c4 |  67717 | 71186 | 74193 | 76660 |
| vs_c4_lg |  106806 | 110220 | 113787 | 113389 |
| vs_c4_lg2 |  118146 | 121168 | 108526 | 124713 |
| vs_bbr_lg |  135313 | 122449 | 141964 | 117425 |
| vs_bbr_lg2 |  121214 | 128986 | 112559 | 121149 |
| vs_cubic_lg |  130672 | 91606 | 125410 | 135951 |
| vs_cubic_lg2 |  124708 | 129258 | 132612 | 126129 |

###  top 90% of RTT + standard deviation for compete tests

|  top 90% of RTT + standard deviation for compete tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  154005 | 156725 | 147826 | 128620 |
| vs_c4 |  152185 | 159709 | 155513 | 146435 |
| vs_cubic |  152658 | 137259 | 159758 | 161246 |
| after_c4 |  148189 | 146770 | 141204 | 146628 |
| before_c4 |  107023 | 97903 | 99392 | 108898 |
| vs_c4_lg |  142532 | 149248 | 147244 | 146006 |
| vs_c4_lg2 |  151239 | 152982 | 137156 | 158239 |
| vs_bbr_lg |  161934 | 156472 | 160010 | 142672 |
| vs_bbr_lg2 |  150952 | 155650 | 143578 | 147234 |
| vs_cubic_lg |  152277 | 113509 | 159776 | 151416 |
| vs_cubic_lg2 |  150050 | 150889 | 159134 | 146915 |

###  average load for compete tests

|  average load for compete tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  81% | 47% | 82% | 78% |
| vs_c4 |  46% | 30% | 54% | 51% |
| vs_cubic |  52% | 14% | 36% | 65% |
| after_c4 |  38% | 21% | 29% | 35% |
| before_c4 |  60% | 53% | 63% | 61% |
| vs_c4_lg |  48% | 41% | 41% | 52% |
| vs_c4_lg2 |  52% | 55% | 39% | 55% |
| vs_bbr_lg |  75% | 49% | 83% | 80% |
| vs_bbr_lg2 |  71% | 61% | 28% | 75% |
| vs_cubic_lg |  66% | 14% | 45% | 70% |
| vs_cubic_lg2 |  77% | 79% | 55% | 72% |

###  top 90% load for compete tests

|  top 90% load for compete tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  82% | 48% | 82% | 78% |
| vs_c4 |  49% | 36% | 60% | 52% |
| vs_cubic |  53% | 15% | 39% | 70% |
| after_c4 |  40% | 23% | 30% | 36% |
| before_c4 |  62% | 56% | 65% | 65% |
| vs_c4_lg |  55% | 54% | 46% | 54% |
| vs_c4_lg2 |  53% | 58% | 41% | 58% |
| vs_bbr_lg |  83% | 50% | 85% | 81% |
| vs_bbr_lg2 |  74% | 69% | 32% | 76% |
| vs_cubic_lg |  69% | 15% | 50% | 78% |
| vs_cubic_lg2 |  81% | 80% | 58% | 74% |


## Buffer bloat

Here the statistics for the buffer bloat test cases.

###  average time for buffer bloat tests

|  average time for buffer bloat tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  12794681 | 12995007 | 12622707 | 12642775 |
| bbloat_c4 |  20994353 | 21363775 | 20716793 | 20935286 |
| bbloat_bbr |  14881484 | 21328671 | 13942815 | 15320599 |
| bbloat_cubic |  20660588 | 21588464 | 20716807 | 20736042 |

###  top 90% time for buffer bloat tests

|  top 90% time for buffer bloat tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  12794853 | 12995526 | 12623909 | 12645052 |
| bbloat_c4 |  21067518 | 21570439 | 20716848 | 21006655 |
| bbloat_bbr |  14946487 | 21416699 | 13990176 | 15431313 |
| bbloat_cubic |  20718563 | 21733982 | 20716830 | 20761314 |

###  average RTT for buffer bloat tests

|  average RTT for buffer bloat tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  91531 | 84840 | 182687 | 113442 |
| bbloat_c4 |  183026 | 99654 | 692469 | 232427 |
| bbloat_bbr |  168268 | 125759 | 233068 | 132447 |
| bbloat_cubic |  712963 | 93024 | 635463 | 551412 |

###  top 90% of RTT + standard deviation for buffer bloat tests

|  top 90% of RTT + standard deviation for buffer bloat tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  137063 | 96518 | 266072 | 158066 |
| bbloat_c4 |  292289 | 146757 | 979722 | 361518 |
| bbloat_bbr |  220249 | 166261 | 349477 | 160200 |
| bbloat_cubic |  1068247 | 122959 | 999167 | 860075 |

###  average load for buffer bloat tests

|  average load for buffer bloat tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  97% | 95% | 98% | 98% |
| bbloat_c4 |  50% | 15% | 31% | 50% |
| bbloat_bbr |  83% | 49% | 89% | 81% |
| bbloat_cubic |  61% | 12% | 48% | 58% |

###  top 90% load for buffer bloat tests

|  top 90% load for buffer bloat tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  97% | 95% | 98% | 98% |
| bbloat_c4 |  52% | 16% | 32% | 53% |
| bbloat_bbr |  84% | 50% | 89% | 82% |
| bbloat_cubic |  61% | 12% | 49% | 58% |


## Wi-Fi

Here the statistics for the wifi test cases.

###  average time for wifi tests

|  average time for wifi tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  3572565 | 5276858 | 4077668 | 4070606 |
| wifi_fade |  5064452 | 5416055 | 5336618 | 5073239 |
| wifi_suspension |  4581278 | 4612549 | 4594461 | 4564955 |
| wifi_bad_bbr |  7136154 | 8315633 | 6790172 | 7065591 |
| wifi_bad_c4 |  10813913 | 12025900 | 11507645 | 8664120 |
| wifi_bad_cubic |  9000711 | 10625081 | 10639380 | 8649554 |

###  top 90% time for wifi tests

|  top 90% time for wifi tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  3793102 | 6767611 | 4362647 | 4530282 |
| wifi_fade |  5291851 | 5681330 | 5535374 | 5357776 |
| wifi_suspension |  4580858 | 4613000 | 4597003 | 4574186 |
| wifi_bad_bbr |  11125580 | 12430498 | 12815995 | 10736818 |
| wifi_bad_c4 |  11991545 | 12674921 | 13075119 | 11249655 |
| wifi_bad_cubic |  11966921 | 12419749 | 13566122 | 11250821 |

###  average RTT for wifi tests

|  average RTT for wifi tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  146549 | 65609 | 92618 | 109023 |
| wifi_fade |  140240 | 124750 | 136634 | 145297 |
| wifi_suspension |  13680 | 13150 | 26539 | 13993 |
| wifi_bad_bbr |  197531 | 194826 | 234190 | 199724 |
| wifi_bad_c4 |  229368 | 203852 | 220966 | 234733 |
| wifi_bad_cubic |  238466 | 224264 | 200377 | 256603 |

###  top 90% of RTT + standard deviation for wifi tests

|  top 90% of RTT + standard deviation for wifi tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  260239 | 145600 | 148298 | 258484 |
| wifi_fade |  206474 | 174746 | 182279 | 207083 |
| wifi_suspension |  31952 | 31843 | 45256 | 32878 |
| wifi_bad_bbr |  324265 | 321729 | 334122 | 330282 |
| wifi_bad_c4 |  323158 | 318008 | 330354 | 331655 |
| wifi_bad_cubic |  321710 | 323608 | 320618 | 337028 |

###  average load for wifi tests

|  average load for wifi tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  100% | 68% | 87% | 84% |
| wifi_fade |  87% | 78% | 80% | 87% |
| wifi_suspension |  90% | 89% | 91% | 91% |
| wifi_bad_bbr |  64% | 54% | 81% | 64% |
| wifi_bad_c4 |  39% | 27% | 35% | 51% |
| wifi_bad_cubic |  46% | 33% | 35% | 50% |

###  top 90% load for wifi tests

|  top 90% load for wifi tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  106% | 95% | 96% | 95% |
| wifi_fade |  90% | 80% | 84% | 90% |
| wifi_suspension |  90% | 89% | 91% | 91% |
| wifi_bad_bbr |  83% | 79% | 114% | 82% |
| wifi_bad_c4 |  48% | 37% | 47% | 69% |
| wifi_bad_cubic |  64% | 52% | 72% | 67% |


## ECN

Here the statistics for the ecn test cases.

###  average time for ecn tests

|  average time for ecn tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4651037 | 4657883 | 4406012 | 4465982 |
| ecn_c4 |  11281528 | 13253617 | 15674266 | 12078995 |
| ecn_cubic |  8526561 | 7780693 | 10792917 | 8246796 |
| ecn_bbr |  13254633 | 13605816 | 17087760 | 13089999 |

###  top 90% time for ecn tests

|  top 90% time for ecn tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4652903 | 4658116 | 4406193 | 4467028 |
| ecn_c4 |  12795636 | 15409855 | 16339295 | 13032878 |
| ecn_cubic |  9324633 | 7685969 | 12007554 | 9023342 |
| ecn_bbr |  13532632 | 13576641 | 17441363 | 13309789 |

###  average RTT for ecn tests

|  average RTT for ecn tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  84426 | 89652 | 116924 | 84661 |
| ecn_c4 |  107818 | 100306 | 97318 | 106047 |
| ecn_cubic |  114973 | 124996 | 128516 | 116735 |
| ecn_bbr |  111076 | 102037 | 95512 | 114200 |

###  top 90% of RTT + standard deviation for ecn tests

|  top 90% of RTT + standard deviation for ecn tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  94982 | 102213 | 129577 | 96364 |
| ecn_c4 |  132703 | 119492 | 117583 | 129210 |
| ecn_cubic |  130867 | 134804 | 135485 | 134465 |
| ecn_bbr |  135172 | 123960 | 115598 | 136495 |


## Media

Here the statistics for the media test cases.

###  average av_latency for media tests

|  average av_latency for media tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| media |  30494 | 30480 | 30430 | 33512 |
| media10 |  44871 | 44874 | 45985 | 45369 |
| media_600fr |  30789 | 30491 | 30439 | 33625 |
| media_short_long |  100827 | 129819 | 101359 | 100792 |
| media_wb |  84656 | 90498 | 78729 | 80315 |
| media_wf |  82615 | 85021 | 85936 | 82068 |
| media_ws |  21790 | 21529 | 22198 | 22955 |
| media_ecn |  32262 | 32365 | 32312 | 34413 |
| media_backload |  32639 | 42494 | 33786 | |

###  top 90% max_latency for media tests

|  top 90% max_latency for media tests| c4 | bbr | cubic | c4_draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| media |  40926 | 35430 | 33110 | 43453 |
| media10 |  87527 | 71128 | 92165 | 71128 |
| media_600fr |  42514 | 35430 | 33148 | 43453 |
| media_short_long |  112263 | 316395 | 115954 | 111153 |
| media_wb |  312122 | 289359 | 264475 | 265337 |
| media_wf |  290827 | 304982 | 308031 | 290560 |
| media_ws |  197834 | 195534 | 197259 | 197821 |
| media_ecn |  50966 | 50996 | 50996 | 47975 |
| media_backload |  82950 | 293871 | 86944 | |


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