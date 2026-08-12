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

|  average time for network events tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4532741 | 4689886 | 4478802 | 4502535 |
| alone_200 |  1117217 | 1221654 | 1146527 | 1120932 |
| alone_1_5M |  21534049 | 21716146 | 21516163 | 21505342 |
| alone_512k |  16246741 | 16209184 | 16184155 | 16173448 |
| low_and_up |  7635085 | 7506828 | 8054838 | 7568928 |
| drop_and_back |  7562693 | 7628092 | 7628534 | 7549810 |
| blackhole |  5629170 | 5811058 | 5695645 | 5591990 |
| short_long |  17895920 | 42277996 | 21364752 | 17536760 |
| satellite |  6807154 | 7472652 | 6704246 | 6807132 |

###  top 90% time for network events tests

|  top 90% time for network events tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  4809635 | 4696426 | 4522497 | 4561489 |
| alone_200 |  1192186 | 1222129 | 1149419 | 1186748 |
| alone_1_5M |  21548532 | 21718518 | 21552308 | 21511156 |
| alone_512k |  16255476 | 16217224 | 16210885 | 16173943 |
| low_and_up |  7640484 | 7511746 | 8071891 | 7570219 |
| drop_and_back |  7637658 | 7635346 | 7632380 | 7569623 |
| blackhole |  5629628 | 5815443 | 5699325 | 5592062 |
| short_long |  17920751 | 43797032 | 21382320 | 17538427 |
| satellite |  6807188 | 7472879 | 6704247 | 6807184 |

###  average RTT for network events tests

|  average RTT for network events tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  104472 | 96175 | 129257 | 113628 |
| alone_200 |  57585 | 45834 | 53608 | 59296 |
| alone_1_5M |  93400 | 54991 | 86183 | 92468 |
| alone_512k |  99663 | 94569 | 95132 | 98346 |
| low_and_up |  110506 | 116922 | 118831 | 109493 |
| drop_and_back |  120686 | 120519 | 129491 | 119770 |
| blackhole |  126500 | 146274 | 168238 | 140658 |
| short_long |  193525 | 193580 | 304877 | 193251 |
| satellite |  601057 | 691996 | 610161 | 601057 |

###  top 90% of RTT + standard deviation for network events tests

|  top 90% of RTT + standard deviation for network events tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| alone |  128421 | 107852 | 167139 | 142636 |
| alone_200 |  85683 | 57162 | 70623 | 88089 |
| alone_1_5M |  98418 | 64652 | 90868 | 96658 |
| alone_512k |  107203 | 104295 | 106150 | 105400 |
| low_and_up |  128226 | 134293 | 146176 | 127962 |
| drop_and_back |  147928 | 150307 | 161324 | 153647 |
| blackhole |  430792 | 500737 | 485330 | 479784 |
| short_long |  231027 | 232519 | 409743 | 230002 |
| satellite |  602313 | 830536 | 621276 | 602375 |


## Competition

Here the statistics for the compete test cases.

###  average time for compete tests

|  average time for compete tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2845383 | 4512911 | 2850821 | 2817345 |
| vs_c4 |  4319537 | 6906224 | 6864460 | 4349648 |
| vs_cubic |  3636721 | 6990789 | 5365048 | 3418203 |
| after_c4 |  6377370 | 6768373 | 7295286 | 6599569 |
| before_c4 |  2677867 | 3989288 | 2906620 | 2633286 |
| vs_c4_lg |  21030243 | 24959409 | 22533505 | 21024837 |
| vs_c4_lg2 |  20826380 | 21169148 | 21464086 | 20961838 |
| vs_bbr_lg |  18520255 | 21100366 | 15585675 | 15608340 |
| vs_bbr_lg2 |  16711900 | 18686365 | 21455573 | 16501414 |
| vs_cubic_lg |  20328662 | 21401604 | 20964829 | 17942252 |
| vs_cubic_lg2 |  18049537 | 15529093 | 20747146 | 17076722 |

###  top 90% time for compete tests

|  top 90% time for compete tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  2852307 | 4580031 | 2875139 | 2821436 |
| vs_c4 |  4486886 | 7889574 | 7218889 | 4521899 |
| vs_cubic |  3818919 | 7089980 | 5585140 | 3787691 |
| after_c4 |  6560594 | 6898353 | 7493964 | 6743285 |
| before_c4 |  2810721 | 5330808 | 3340007 | 2734795 |
| vs_c4_lg |  21167118 | 38051896 | 23650057 | 21132908 |
| vs_c4_lg2 |  21080264 | 21229760 | 21821599 | 21040360 |
| vs_bbr_lg |  19924057 | 21140116 | 15871732 | 15857330 |
| vs_bbr_lg2 |  16683416 | 19011806 | 22180597 | 16599500 |
| vs_cubic_lg |  20950527 | 21754587 | 21303907 | 20324018 |
| vs_cubic_lg2 |  18493023 | 15795141 | 21059196 | 17505908 |

###  average RTT for compete tests

|  average RTT for compete tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  102396 | 125593 | 132353 | 104812 |
| vs_c4 |  118180 | 107068 | 128505 | 114721 |
| vs_cubic |  133077 | 117847 | 139546 | 136494 |
| after_c4 |  113661 | 108113 | 99761 | 115545 |
| before_c4 |  74432 | 69165 | 76288 | 76660 |
| vs_c4_lg |  108337 | 97098 | 115600 | 113389 |
| vs_c4_lg2 |  122218 | 116421 | 112536 | 124713 |
| vs_bbr_lg |  125485 | 127266 | 145907 | 117425 |
| vs_bbr_lg2 |  117809 | 136587 | 116359 | 121149 |
| vs_cubic_lg |  131683 | 94450 | 129063 | 135951 |
| vs_cubic_lg2 |  123806 | 131710 | 136183 | 126129 |

###  top 90% of RTT + standard deviation for compete tests

|  top 90% of RTT + standard deviation for compete tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  122588 | 157003 | 162885 | 128620 |
| vs_c4 |  153063 | 133940 | 156286 | 146435 |
| vs_cubic |  157438 | 151502 | 164038 | 161246 |
| after_c4 |  151404 | 135666 | 123680 | 146628 |
| before_c4 |  104524 | 95741 | 108699 | 108898 |
| vs_c4_lg |  138184 | 125706 | 151824 | 146006 |
| vs_c4_lg2 |  150109 | 154944 | 141337 | 158239 |
| vs_bbr_lg |  154821 | 159904 | 162703 | 142672 |
| vs_bbr_lg2 |  141804 | 155610 | 151989 | 147234 |
| vs_cubic_lg |  149988 | 120205 | 162108 | 151416 |
| vs_cubic_lg2 |  143974 | 149740 | 159921 | 146915 |

###  average load for compete tests

|  average load for compete tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  77% | 47% | 78% | 78% |
| vs_c4 |  50% | 25% | 33% | 51% |
| vs_cubic |  61% | 18% | 41% | 65% |
| after_c4 |  37% | 22% | 25% | 35% |
| before_c4 |  59% | 41% | 56% | 61% |
| vs_c4_lg |  51% | 28% | 33% | 52% |
| vs_c4_lg2 |  58% | 49% | 38% | 55% |
| vs_bbr_lg |  68% | 50% | 80% | 80% |
| vs_bbr_lg2 |  74% | 67% | 39% | 75% |
| vs_cubic_lg |  60% | 18% | 47% | 70% |
| vs_cubic_lg2 |  68% | 81% | 56% | 72% |

###  top 90% load for compete tests

|  top 90% load for compete tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| vs_bbr |  77% | 47% | 79% | 78% |
| vs_c4 |  52% | 26% | 36% | 52% |
| vs_cubic |  68% | 18% | 46% | 70% |
| after_c4 |  41% | 22% | 27% | 36% |
| before_c4 |  62% | 52% | 64% | 65% |
| vs_c4_lg |  54% | 32% | 37% | 54% |
| vs_c4_lg2 |  60% | 56% | 41% | 58% |
| vs_bbr_lg |  72% | 50% | 81% | 81% |
| vs_bbr_lg2 |  76% | 69% | 50% | 76% |
| vs_cubic_lg |  66% | 19% | 51% | 78% |
| vs_cubic_lg2 |  70% | 82% | 61% | 74% |


## Buffer bloat

Here the statistics for the buffer bloat test cases.

###  average time for buffer bloat tests

|  average time for buffer bloat tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  12965334 | 13007783 | 12626198 | 12642775 |
| bbloat_c4 |  20922325 | 31496642 | 20715707 | 20935286 |
| bbloat_bbr |  15885935 | 21339919 | 14427809 | 15320599 |
| bbloat_cubic |  20737336 | 21604224 | 20716716 | 20736042 |

###  top 90% time for buffer bloat tests

|  top 90% time for buffer bloat tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  12965477 | 13007879 | 12626285 | 12645052 |
| bbloat_c4 |  21007866 | 39323391 | 20715748 | 21006655 |
| bbloat_bbr |  16120409 | 21420708 | 14543367 | 15431313 |
| bbloat_cubic |  20757830 | 21858168 | 20717193 | 20761314 |

###  average RTT for buffer bloat tests

|  average RTT for buffer bloat tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  99976 | 84752 | 229743 | 113442 |
| bbloat_c4 |  194283 | 90477 | 660173 | 232427 |
| bbloat_bbr |  114217 | 124516 | 266604 | 132447 |
| bbloat_cubic |  599487 | 97424 | 647535 | 551412 |

###  top 90% of RTT + standard deviation for buffer bloat tests

|  top 90% of RTT + standard deviation for buffer bloat tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  144800 | 95929 | 322658 | 158066 |
| bbloat_c4 |  307278 | 109771 | 940428 | 361518 |
| bbloat_bbr |  138418 | 163039 | 408660 | 160200 |
| bbloat_cubic |  950975 | 137116 | 1039774 | 860075 |

###  average load for buffer bloat tests

|  average load for buffer bloat tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  95% | 95% | 98% | 98% |
| bbloat_c4 |  51% | 22% | 34% | 50% |
| bbloat_bbr |  78% | 48% | 86% | 81% |
| bbloat_cubic |  59% | 15% | 53% | 58% |

###  top 90% load for buffer bloat tests

|  top 90% load for buffer bloat tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| bbloat |  95% | 95% | 98% | 98% |
| bbloat_c4 |  53% | 23% | 34% | 53% |
| bbloat_bbr |  79% | 49% | 86% | 82% |
| bbloat_cubic |  60% | 15% | 54% | 58% |


## Wi-Fi

Here the statistics for the wifi test cases.

###  average time for wifi tests

|  average time for wifi tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  3998490 | 5218367 | 4071761 | 4070606 |
| wifi_fade |  5054668 | 5412217 | 5324077 | 5073239 |
| wifi_suspension |  4566286 | 4615814 | 4600130 | 4564955 |
| wifi_bad_bbr |  6213267 | 7594183 | 7827322 | 7065591 |
| wifi_bad_c4 |  10245859 | 12482642 | 12338113 | 8664120 |
| wifi_bad_cubic |  7946052 | 9224321 | 9925972 | 8649554 |

###  top 90% time for wifi tests

|  top 90% time for wifi tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  4442287 | 7250367 | 4307986 | 4530282 |
| wifi_fade |  5312593 | 5594411 | 5513416 | 5357776 |
| wifi_suspension |  4574738 | 4616322 | 4600822 | 4574186 |
| wifi_bad_bbr |  7819416 | 11939508 | 12716387 | 10736818 |
| wifi_bad_c4 |  11775007 | 13871562 | 13698529 | 11249655 |
| wifi_bad_cubic |  10558771 | 12212696 | 14014123 | 11250821 |

###  average RTT for wifi tests

|  average RTT for wifi tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  107944 | 69595 | 93496 | 109023 |
| wifi_fade |  143974 | 124720 | 137841 | 145297 |
| wifi_suspension |  13627 | 13461 | 26506 | 13993 |
| wifi_bad_bbr |  198381 | 206225 | 230838 | 199724 |
| wifi_bad_c4 |  267859 | 212748 | 227114 | 234733 |
| wifi_bad_cubic |  263576 | 248921 | 207139 | 256603 |

###  top 90% of RTT + standard deviation for wifi tests

|  top 90% of RTT + standard deviation for wifi tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  259264 | 146732 | 147174 | 258484 |
| wifi_fade |  206854 | 174076 | 185971 | 207083 |
| wifi_suspension |  32686 | 34220 | 45311 | 32878 |
| wifi_bad_bbr |  339631 | 344001 | 335892 | 330282 |
| wifi_bad_c4 |  346680 | 338303 | 346896 | 331655 |
| wifi_bad_cubic |  343034 | 353578 | 322096 | 337028 |

###  average load for wifi tests

|  average load for wifi tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  85% | 71% | 88% | 84% |
| wifi_fade |  87% | 78% | 81% | 87% |
| wifi_suspension |  91% | 89% | 91% | 91% |
| wifi_bad_bbr |  72% | 66% | 68% | 64% |
| wifi_bad_c4 |  61% | 35% | 36% | 51% |
| wifi_bad_cubic |  57% | 62% | 43% | 50% |

###  top 90% load for wifi tests

|  top 90% load for wifi tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| wifi_bad |  95% | 95% | 95% | 95% |
| wifi_fade |  90% | 80% | 85% | 90% |
| wifi_suspension |  91% | 89% | 91% | 91% |
| wifi_bad_bbr |  82% | 98% | 101% | 82% |
| wifi_bad_c4 |  71% | 45% | 54% | 69% |
| wifi_bad_cubic |  72% | 121% | 80% | 67% |


## ECN

Here the statistics for the ecn test cases.

###  average time for ecn tests

|  average time for ecn tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4542617 | 4670033 | 4460543 | 4465982 |
| ecn_c4 |  11016727 | 16471083 | 14103913 | 12078995 |
| ecn_cubic |  8581721 | 9944454 | 13400852 | 8246796 |
| ecn_bbr |  13183391 | 13239150 | 16973882 | 13089999 |

###  top 90% time for ecn tests

|  top 90% time for ecn tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  4542787 | 4670724 | 4457944 | 4467028 |
| ecn_c4 |  11924436 | 17168063 | 14241970 | 13032878 |
| ecn_cubic |  9372475 | 10665181 | 13956145 | 9023342 |
| ecn_bbr |  13359522 | 13366333 | 17552699 | 13309789 |

###  average RTT for ecn tests

|  average RTT for ecn tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  84255 | 89458 | 121748 | 84661 |
| ecn_c4 |  108018 | 95711 | 103830 | 106047 |
| ecn_cubic |  112801 | 124459 | 114591 | 116735 |
| ecn_bbr |  112144 | 101606 | 94807 | 114200 |

###  top 90% of RTT + standard deviation for ecn tests

|  top 90% of RTT + standard deviation for ecn tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| ecn |  95650 | 101684 | 139163 | 96364 |
| ecn_c4 |  128589 | 112609 | 124232 | 129210 |
| ecn_cubic |  130449 | 136000 | 137983 | 134465 |
| ecn_bbr |  136065 | 123327 | 114368 | 136495 |


## Media

Here the statistics for the media test cases.

###  average av_latency for media tests

|  average av_latency for media tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| media |  33511 | 33427 | 33511 | 33512 |
| media10 |  45380 | 44994 | 47754 | 45369 |
| media_600fr |  33625 | 33545 | 33628 | 33625 |
| media_short_long |  100792 | 133969 | 100774 | 100792 |
| media_wb |  80584 | 85465 | 74388 | 80315 |
| media_wf |  82236 | 88155 | 81738 | 82068 |
| media_ws |  22948 | 21644 | 22133 | 22955 |
| media_ecn |  34413 | 34481 | 34716 | 34413 |

###  top 90% max_latency for media tests

|  top 90% max_latency for media tests| c4 | bbr | cubic | draft-04 |
| --------- | ---:| ---:| ---:| ---:|
| media |  43453 | 43453 | 43453 | 43453 |
| media10 |  71128 | 71128 | 92163 | 71128 |
| media_600fr |  43453 | 43453 | 43453 | 43453 |
| media_short_long |  111153 | 334491 | 110858 | 111153 |
| media_wb |  249472 | 289267 | 259570 | 265337 |
| media_wf |  280600 | 349285 | 304248 | 290560 |
| media_ws |  197821 | 195521 | 197821 | 197821 |
| media_ecn |  47975 | 50996 | 50996 | 47975 |

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