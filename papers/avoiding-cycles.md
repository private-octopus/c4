# Avoiding cycles in parameters

In the design of C4, we are of course trying to avoid cycles
or rather spirals. The typical spiral happens when a
parameter is picked from a measurement, but the next
measurement depends on the new value of this parameter.
We have a concrete example of that with our erstwhile
attempt to use the "max observed number of bytes in
flight" to set the CWND of the path.

C4 will sometimes be is operating on a path with high jitter,
such as a wifi path subject to interference of other nearby
networks. In such networks, we try to set a pacing rate that
reflects the estimated data rate of the path, and a congestion
window that is sufficient to avoid the transmission stalling
if a jitter event suddenly increases the transmission delay.
Ideally, we would set the CWND to the product of the data
rate and the longest jitter that we expect to absorb. In
a first design, we set:
~~~
CWND = alpha*nominal_CWND + max_bytes_in_flight/2
~~~

