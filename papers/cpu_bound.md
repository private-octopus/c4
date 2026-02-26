# Performance of C4 when CPU bound

Back in November 2025, when doing tests of C4 on a loopback address,
I observed that C4 achieved lower data rates than Cubic or even BBR.
Since "performance under loopback" was not a high priority scenario,
I filed that in the long pile of issues to deal with later. Then, in
early January 2026, I read a preprint of a paper by Kathrin Elmenhorst
and Nils Aschenbruck titled "2BRobust -- Overcoming TCP BBR Performance
Degradation in Virtual Machines under CPU Contention"
(see: https://arxiv.org/abs/2601.05665).
In that paper, they point out that BBR achieves lower than nominal
performance when running on VMs under high CPU load, and
trace that to pacing issues. Pacing in an application process involves
periodically waiting until the pacing system acquires enough tokens.
If the CPU is highly loaded, the system call can take longer than the
specified maximum wait time, and pacing thus slows the connection more
than expected. In such conditions, they suggest increasing the programmed
pacing rate above the nominal rate, and show that it helps performance.

C4 is different from BBR, but like BBR it relies on pacing. After reading
the paper, I wondered whether similar fixes might be working for C4 as
well, and I quickly set up a series of tests.

## First diagnostic

As mentioned, the first performance tests on C4 over the loopback
interface showed lower performance than both BBR and C4. I repeated
the tests after modifying the test program to capture a log of the
connection in memory, so as to interfere as little as possible
with performance.

The memory log showed that the connection was almost always
saturating the allocated CWND. That's not the expected behavior
with C4. The CWND is computed as the product of the "nominal
rate" and the "nominal max RTT", which is supposedly higher
than the average RTT. When that works correctly, traces show
the bytes in flight constantly lower than the program CWND.
The CWND is mostly used as some kind of "safety belt", to limit
excess sending in abnormal network conditions.

## First fix, set a floor on max RTT

Looking at the details of the logs showed that the pacing rate was
set at a reasonable value, but that the "nominal max RTT" was
closely tracking the RTT variations, which oscillated between a few
microseconds and up to 1ms. C4 has an adaptive algorithm to draw down
the nominal max RTT over time, and after a few RTT that value was
getting small, resulting in small values of CWND and low performance.

Our first fix was to set a 1ms floor on max RTT. This will limit
the effect of measurement errors and ensure that the CWND always
enable 1ms of continuous transmission. We can do this fix safely
because C4 enforces pacing at or near the nominal rate, and thus
avoids excessive queues that could lead to packet losses.

The effect was immediate. Before the fix, the throughput values observed
during loopback tests were both very low and variable. After the
fix of the max RTT, the observed throughputs became systematically
better than what we observe with BBR on the same computer, and within
88% of the values achieved with Cubic.

## Could it be a pacing issue?

Bringing the performance of C4 on loopback within 88% of the performance of
Cubic was a great improvement, but why not improve further and reach parity
or better?

Kathrin Elmenhorst and Nils Aschenbruck observed that the poor performance
of BBR is some scenarios was due to excessive pacing. C4 uses a pacing
logic very similar to BBR, and we see that both C4 and BBR performed less
well than Cubic in loopback tests. Using memory logs, we verified that
the rate estimates were generally lower than the pacing rate, while the
theory assumed that they would be almost the same. This is a strong
indication that excessive wait time in system calls makes pacing too
slow when the CPU is overloaded.

We considered measuring these waiting times, and then developing an automated
way to increase the pacing rate above the nominal rate when the waiting times
are excessive. The problem is that such adaptive systems are difficult to
properly tests, and the time allocated to completing this study was limited.
Instead, we use a simple series of tests to check what happens if we increase
the pacing rate.

We tested pacing at a fraction above the nominal rate, and we tried three
fractions: 1/32, 1/16, and 3/64. Each test showed an improvement. The smaller
fraction, 1/32, was bringing the performance close to those of Cubic. The
larger fraction, 1/16, was improving on that but was probably excessive:
we observed a large amount of packet losses, due to the building of
queues. The middle ground actually improved over both previous tests. The
observed throughput was better than both the 1/32 and the 1/16 test: higher
throughput, but not so high that it would cause excessive losses that slow
the transmission. In fact, the performance were now better with C4 than
with Cubic.

![graph showing performance on loopback for C4, Cubic and BBR](./perf-loopback-fix-cpu.png "Performance on loopback for C4, Cubic and BBR")

The graph above shows the resulting performance measured on loopback on a
Windows laptop. We did three tests for each congestion control variant,
each time measuring the time needed to transfer 10GB and computing the
resulting throughput. In the first series of tests, before improvements,
C4's performance was very bad. After setting a floor to the max RTT,
performance became better than BBR, but still lower than Cubic. After
adding a 3/64th increase to the pacing rate, performance became better
than Cubic.

The C4 code in picoquic was fixed to always apply the 1ms floor to the RTT,
but to only apply the 3/64th increase if the average RTT is less than 1ms.
This restriction ensure that the increase is only used in the narrow
conditions under which it was tested, without impeding other scenarios.
We could replace that by a more general solution in the future,
once we have validated an automated algorithm to detect and compensate for
excessive pacing.