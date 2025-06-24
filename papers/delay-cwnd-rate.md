# Delay, Congestion Window and Data Rate

The first version of C4 used the Congestion Window (CWND) as the primary
control. This arbitrary design decision was motivated by a desire for
simplicity, and especially by a reaction to the increasing complexity
of BBR variants that use a combination of the pacing rate and the congestion
window, and also a combination of short term and long term controls.
However, the simplification derive from chosing exactly one control
variable instead of combining several of them. We would get
the same simplicity if we used the pacing rate as the primary
control.

There are potential gains for either solution. Control based on CWND
allows us to absorb slight variations in the data rate, and is very
much in line with historic algorithms like RENO or Cubic. Control
based on the pacing rate is more similar to the BBR experience, and
allows for more continuity in presence of delay jitter.

In order to allow comparisons, we add a configuration parameter in
C4, so we can compare two possible variants.

## Controling the sending rate

In both cases, we control the sending rate using a combination of pacing and
congestion windows limit.

* in the CWND variant, we use the CWND as the main variable, and we set the
  pacing rate to a loose value:
~~~
   cwnd = alpha * nominal_cwnd
   pacing_rate = gamma_r * nominal_cwnd / min_rtt
~~~

* in the rate variant, we use the pacing rate as the main variable, and we set
  CWND to a loose value:
~~~
   pacing_rate = alpha * nominal_rate
   cwnd = gamma_w * pacing_rate * min_rtt
~~~

The coefficient `alpha` depends on the state of the algorithm:

- during cruising, `alpha=1`,
- during startup, `alpha=2`,
- during probes, `alpha=5/4`.

The coefficients `gamma_r` and `gamma_w` are the `loosening factors` ensuring that
in nominal conditions
the secondary control does not get in the way of the primary control:

- in the CWND variant we normally set `gamma_r = 5/4`, which allows some higher
  instantaneous rates when the transmission is not limited by CWND,
- in the rate variant, we normally set `gamma_w = 2`, which allows C4 to absorb
  an amount of delay jitter similar to the `min_rtt`.

We may need to adapt these `gamma` cofficients based on experience,
maybe mirroring the decisions in the BBR draft. For example, in the
startup phase, we already apply a large cofficient `alpha` to allow exploration
of the nework capacity. It may not be wise to increase the `gamma`
coefficients beyond that.

## Upper Capacity Discovery

In C4, capacity increases are discovered either during startup or during periodic probing periods.

* in the CWND variant, we compute a nominal CWND with the formula:
~~~
   rtt_target = min_rtt * (1 + 5/100)
   observed_cwnd = amount-of-data-acked-in-RTT
   corrected_cwnd = observed_cwnd * rtt_target / rtt_sample
   nominal_cwnd = max (nominal_cwnd, corrected_cwnd)
~~~
* in the rate variant, we can apply similar formula:
~~~
   observed_cwnd = amount-of-data-acked-in-RTT
   observed_rate = observed_cwnd / rtt_sample
   nominal_rate = max (nominal_rate, corrected_rate)
~~~
These two formulas are broadly equivalent, but the rate variant does not
depend on `min_rtt`.

## Lower Rate Discovery

In C4 variant, we discover that the sending rate is too high by noticing
congestion events, such as:

- sustained packet losses,
- high rate of ECN `congestion experienced` marks,
- excessive delays.

When a congestion event is observed, we need to reduce the control variable:

- in the CWND variant, we reduce the `nominal_cwnd`:
~~~
   nominal_cwnd = beta * nominal_cwnd
~~~
- in the rate variant, we reduce the `nominal_rate`:
~~~
   nominal_rate = beta * nominal_rate
~~~

The two behaviors are equivalent.

## Special case, exit startup

C4 is programmed to exit startup when it has almost discovered the network capacity.
This is implemented in similar ways in the CWND and rate variants:

- in the CWND variant, exit start-up if the `nominal_cwnd` does not
  increase for 3 consecutive RTT,
- in the rate variant, exit start-up if the `nominal_rate` does not
  increase for 3 consecutive RTT.

## Special case, competition with Cubic

C4 detects that it is competing with a `greedy` congestion control algorithm
if it observed 4 successive `delay based` congestion notifications while
in `crusing` mode. In that case, it switches to the `pig_war` mode,
and stops reacting to delay-based congestion notifications. It will
exit the `pig_war` mode if it at passes 3 successive `push` states
without noticing congestion. 

Ths logic remains the same whether the control is via CWND or via pacing rate.

## Special case, chaotic jitter

Some data links will exhibit very large delay jitter
under some conditions. For example, when adjacent Wi-Fi networks collide
on the same frequency band, the low level congestion avoidance and
packet retransmission mechanisms can sometimes result in latencies of several
hundred milliseconds, far larger than the nominal min_rtt of a few milliseconds.

C4 monitors the maximum RTT to detect these conditions. It computes a max
observed RTT per "era" (i.e., roundtrip), and maintains a estimator of
the `nominal_max_rtt` as:
~~~
   nominal_max_rtt = (7 * nominal_max_rtt + era_max_rtt) / 8;
~~~
If the estimated value raises above twice the min_rtt, C4 will
enter the `chaotic_jitter` mode. C4 will exit that mode if
the `nominal_max_rtt` becomes lower than 1.5 times the min_rtt.

In the `chaotic_jitter` mode:

* the detection of delay-based congestion events is suspended,
* if the control is based on CWND, the computation of the `corrected_cwnd` used
  when receiving acknowledgements changes to incorporate the larger target
  delay:
~~~
   if chaotic_jitter:
      rtt_target = (3*min_rtt + nominal_max_rtt)/4
   else:
      rtt_target = min_rtt * (1 + 5/100)
   observed_cwnd = amount-of-data-acked-in-RTT
   corrected_cwnd = observed_cwnd * rtt_target / rtt_sample
   nominal_cwnd = max (nominal_cwnd, corrected_cwnd)
~~~
* if the control is based on the pacing rate, the derivation of CWND from
  the pacing rate changes to incorparate the larger target:
~~~
   pacing_rate = alpha * nominal_rate
   if chaotic_jitter:
      rtt_target = (3*min_rtt + nominal_max_rtt)/4
   else:
      rtt_target = min_rtt * (1 + 5/100)
   cwnd = gamma_w * pacing_rate * rtt_target
~~~

Note: the `min_rtt` used in these formulas is a filtered minimum,
set to the highest value of the last 7 RTT samples. This maight need
to be revised.
