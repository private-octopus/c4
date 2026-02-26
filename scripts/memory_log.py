#
# usage python memory_log.py memlog.csv
# columns are:
# ['current_time', 'send_sequence', 'highest_ack', 'high_ack_time', 'latest_time_ack', 
# 'cwin', 'one_way_delay', 'rtt_sample', 'smoothed_rtt', 'rtt_min',
# 'bw_e', 'pacing_rate', 'recv_rate', 'send_mtu', 'nb_retrans', 'nb_spurious', 
# 'cwin_blocked', 'flow_blocked', 'stream_blocked', 'cc_state', 'cc_param',
# 'peak_bandwidth_estimate', 'bytes_in_transit', 'bwe_path_limited']
#
# In the first experiment, we want to track:
# 'cwin', 'bytes_in_transit',
# 

import sys
import json
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np


def max_x(v, m):
    if v[0] > m:
        v[0] = m

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
        l6 = "estimate (B/s), " + df_names[i]
        l7 = "max_RTT, " + df_names[i]
        mb = [ 0 ]
        mw = [ 0 ]
        df.apply(lambda x: max_x(mb, x["bytes_in_transit"]), axis=1)
        df.apply(lambda x: max_x(mw, x["cwin"]), axis=1)

        print("Max bytes in transit = " + str(mb[0]))
        print("Max CWIN = " + str(mw[0]))

        tdfs[i].plot.scatter(ax=axes[0], x="current_time", y="bytes_in_transit", s=15, marker=markers[i], xlabel="time(us)", ylabel="bytes", alpha=0.5, color=colors2[i], label=l2)
        tdfs[i].plot.line(ax=axes[0], x="current_time", y="cwin", linewidth=2, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="bytes", color=colors1[i], label=l1)       
        tdfs[i].plot.line(ax=axes[1], x="current_time", y="rtt_sample", linewidth=2, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="us", color=colors1[i], label=l3)     
        tdfs[i].plot.line(ax=axes[1], x="current_time", y="rtt_min", linewidth=1, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="us", color=colors2[i], label=l4)
        tdfs[i].plot.line(ax=axes[1], x="current_time", y="cc_param", linewidth=1, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="us", color=colors3[i], label=l7)
        tdfs[i].plot.line(ax=axes[2], x="current_time", y="pacing_rate", linewidth=2, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="bytes/s", color=colors1[i], label=l5) 
        tdfs[i].plot.line(ax=axes[2], x="current_time", y="bw_e", linewidth=2, linestyle=dashes[i], alpha=0.75, xlabel="time(us)", ylabel="bytes/s", color=colors2[i], label=l6)         
    #plt.legend(legends)
    if len(f_name) == 0:
        plt.show()
    else:
        plt.savefig(f_name)


# main

df = pd.read_csv(sys.argv[1], skipinitialspace=True)
print(df.columns.tolist())
dfg = df[df['bw_e'] > 0]
dfg.to_csv("mem_subset.csv")
tdfs = [ dfg ]
df_names = [ "c4" ]
trace_graphs(tdfs, df_names, f_name="")