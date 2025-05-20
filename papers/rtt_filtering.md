# RTT Filtering

C4 uses the same RTT min filtering that was developed in Picoquic for Hystart.
By default, the "RTT min" is not set to the very latest value, but to the
maximum of the last 7 measurements. This filtering was developed to avoid
false exits from Hystart based an a single measurement that could be
impacted by jitter.

## Impact on Hystart

The algorithm was defined before the ACK scheduling extensions. When the
extensions are in place, picoquic tries to reduce the frequency of
acknowledgements, typically to 4 to 8 per RTT. Thus, there will be 1 or 2 RTT
elapsed before the filtered value actually changes. That means, for example,
that if the delay increases due to excess queuing, the Hystart algorithm
will only react after 1 or 2 RTT, by which time the congestion window
may have increased by a factor 2 to 4.

## Larger RTT in presence of jitter

In presence of jitter, we will see successive packets experiencing different
RTT. The "best of 7" formula will tend to smooth the jitter, by design.
The probability of this maximum falling in the lowest 10% of values
will be 10-7. In all likelyhood, most measurements will return a value
in the largest 90%. This seems like a lot of filtering!

The main consequence of this filtering is that Hystart will have to wait
for exiting until the RTT has increased so much that 90% of the samples
return an excessive value. 

## Impact on C4 congestion control

The simulations show that C4 works with the current filtering, or
despite the present filtering. The filtering leads to overestimating
the min RTT, and thus operating with a larger window than necessary.
It also leads to slower reaction to congestion, which in turn will
tend to create larger queues than necessary.

## Impact on detection of RTT increase

We try to estimate whether the RTT increased by lowering the sending
rate and checking whether that results in a lower RTT or not. The
sending rate is normally lowered for 1 RTT. If we only receive 4
acknowledgements during the slowdown period, the largest RTT in the
max of 7 filter will be dominated by 3 measurements when the transmission
was going at full rate. If we want the measurements to reflect the
lower transmission rate, we have to either increase the duration of
the slowdown to capture at least 7 acks, increase the ACK frequency
to transmit more ACKs faster, or reduce the length of the filter.
However, the congestion control algorithm should not depend on
the particular implementation of RTT filtering in picoquic --
that would be fragile. It is simpler and more robust to require
that the "slowdown"
periods used for RTT tracking last at least 2 RTT. (see {{./tracking_delays.md}})









