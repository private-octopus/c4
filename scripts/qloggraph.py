# parse a qlog file
#
# we want to draw graphs combining several logs, to show the results
# of competition between various congestion control algorithms
#
# we would like the logs to show:
# - CWIN
# - Bytes in flight
# - latest RTT sample
# - cumulative data
#
# we are getting these data from parsing the qlog file.

import sys
import json
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import qlogparse

def trace_one_graph(trace):
    tdf = pd.DataFrame(trace.cc_log, columns=qlogparse.cc_state.cc_headers())
    # Prepare a subtrace with cwnd and bytes in flight
    axa = tdf.plot.scatter(x="event_time", y="cwnd", alpha=0.5, logx=False, logy=False, color="blue")
    tdf.plot.scatter(ax=axa, x="event_time", y="bytes_in_flight", xlabel="time(us)", ylabel="bytes", alpha=0.5, color="orange")
    tdf.plot.scatter(ax=axa, x="event_time", y="pacing_rate", xlabel="time(us)", ylabel="rate", alpha=0.5, color="orange")
    plt.legend(["cwnd", "bytes_in_flight", "pacing_rate"])  
    plt.show()

def trace_graphs(tdfs, df_names, f_name=""):
    colors1 = ["blue", "green", "violet", "red", "orange"]
    colors2 = ["turquoise", "lime", "magenta", "pink", "yellow" ]
    colors3 = ["black", "gray", "brown", "blue", "magenta" ]
    dashes = ['solid', 'dashed', 'dashdot', 'dotted', 'dotted' ]
    markers = [ 'o', '+', 'x', '^', '.' ]
    i_max = len(tdfs)
    if i_max > 5:
        i_max = 5
    legends = []
    # Prepare a subtrace with cwnd and bytes in flight
    fig, axes = plt.subplots(3, gridspec_kw={'height_ratios': [1, 1, 1]}, figsize=(8, 6), sharex=True, layout='constrained')
    axes.flatten()
    #fig.tight_layout()
    for i in range(0, i_max):
        l1 = "cwin, " + df_names[i]    
        l2 = "bytes in flight, " + df_names[i]
        l3 = "rtt, " + df_names[i]
        l4 = "min rtt, " + df_names[i]
        l5 = "pacing (B/s), " + df_names[i]
        tdfs[i].plot.scatter(ax=axes[0], x="event_time", y="bytes_in_flight", s=15, marker=markers[i], xlabel="time(us)", ylabel="bytes", alpha=0.5, color=colors2[i], label=l2)
        tdfs[i].plot.line(ax=axes[0], x="event_time", y="cwnd", linewidth=2, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="bytes", color=colors1[i], label=l1)       
        tdfs[i].plot.line(ax=axes[1], x="event_time", y="latest_rtt", linewidth=2, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="us", color=colors1[i], label=l3)     
        tdfs[i].plot.line(ax=axes[1], x="event_time", y="min_rtt", linewidth=1, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="us", color=colors2[i], label=l4)
        tdfs[i].plot.line(ax=axes[2], x="event_time", y="pacing_Bps", linewidth=2, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="bytes", color=colors1[i], label=l5)       
    #plt.legend(legends)
    if len(f_name) == 0:
        plt.show()
    else:
        plt.savefig(f_name)


# test part of the program
# assume each argument is a qlog file

tdfs = []
tdf_names = []

for i in range(2, len(sys.argv)):
    trc = qlogparse.qlog_parse(sys.argv[i])
    tdf = pd.DataFrame(trc[0].cc_log, columns=qlogparse.cc_state.cc_headers())
    tdf ['pacing_Bps'] = tdf['pacing_rate']/8

    tdfs.append(tdf)
    if i == 1:
        tdf_names.append("main")
    elif i == 2 and len(sys.argv) == 3:
        tdf_names.append("background")
    else:
        tdf_names.append("background_" + str(i-1))
trace_graphs(tdfs, tdf_names, f_name=sys.argv[1])

