# Simulation of wifi losses.
#
# We suspect that the WiFi delays are caused by the WiFi retry process,
# which includes a form of exponential backoff. We will try to
# explain the process by analyzing traces, and then seeing whether
# a simple simulation process will produce the same dispersion of delays
# and losses.
from email.mime import base
import random
import sys
import os
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import math

class trace_event:
    def __init__(self, number, sent, received, echo, rtt, up_t, down_t, phase):
        self.number = number
        self.sent =  sent
        self.received = received
        self.echo = echo
        self.rtt = rtt
        self.up_t = up_t
        self.down_t = down_t
        self.phase = phase

        if echo == 0 and down_t > sent:
            self.echo = down_t
            self.down_t = self.rtt - self.up_t

    def reset_phase(self, phase):
        # up_t = (received + phase) - sent_at;
        # down_t = rtt - up_t;
        self.phase = phase
        if self.received > 0:
            phased = self.received - phase
            self.up_t = phased - self.sent
        if self.rtt > 0:
            self.down_t = self.rtt - self.up_t

    def columns():
        return [ "number","sent", "received", "echo", "rtt", "up_t", "down_t", "phase", "is_lost" ]

    def row(self):
        return([self.number, self.sent, self.received, self.echo, self.rtt, self.up_t, self.down_t, self.phase ])


class time_series:
    def __init__(self, name):
        self.latency = []
        self.stamp = []
        self.max_latency = 0
        self.min_latency = 99999999
        self.repeated = 0
        self.name = name
        self.alpha = 0
        self.pareto_likely = 0

    def append(self, latency, stamp):
        if latency > self.max_latency:
            self.max_latency = latency
        if latency < self.min_latency:
            self.min_latency = latency
        self.latency.append(latency)
        self.stamp.append(stamp)
    
    def series_stats(self):
        print("min latency(" + self.name + "): " + str(self.min_latency))
        print("max latency(" + self.name + "): " + str(self.max_latency))
    
    def pareto_eval(self):
        n = 0
        sigma_log = 0.0
        log_alpha_xm = 0
        previous_masque = 0
        if self.min_latency > 0:
            for latency in self.latency:
                if latency > 0:
                    n += 1
                    sigma_log += math.log(latency)
            n_log_xm = n*math.log(self.min_latency)
            sigma_log_q = sigma_log - n_log_xm 
            self.alpha = n/sigma_log_q
            self.pareto_likely = n*math.log(self.alpha) - self.alpha*n_log_xm - \
               (self.alpha + 1)*sigma_log
        print("n (" + self.name + "): " + str(n))
        print("pareto alpha (" + self.name + "): " + str(self.alpha))
        print("pareto_likely (" + self.name + "): " + str(self.pareto_likely))

    # The empirical model is computed as a set of steps.
    # Each step 2x the latency of the first one.
    # For each step, we compute the initial population (starts at N) and
    # the remaining population (loss rate at that step)
    def model_match(self, F, step=0):
        i = 1
        old_remain = self.latency
        if step == 0:
            step = self.min_latency
            if step < 400:
                print("Min latency " + str(step) + " too low, using 400 instead.")
                step = 400
        limit = 2*step
        drop = []
        F.write(self.name + "," + str(len(old_remain)) + "," + str(step))
        while len(old_remain) > 0:
            remain = []
            for latency in old_remain:
                if latency >= limit:
                    remain.append(latency)
            dropped = len(remain)
            drop_rate = dropped/len(old_remain)
            drop.append(drop_rate)
            F.write(',' + str(drop_rate))
            step *= 2
            limit += step
            old_remain = remain
            i += 1
        while i <= 10:
            F.write(',0')
            i += 1
        F.write("\n")
        return(drop)

    def get_df(self):
        t = []
        for i in range(0, len(self.latency)):
            t.append([self.stamp[i], self.stamp[i] + self.latency[i], self.latency[i] ])
        df = pd.DataFrame(t, columns=['send_time', 'recv_time', 'rtt'])
        return(df)

    def get_hist(self, step):
        hist = [ self.name ]
        for i in range(0, len(self.latency)):
            bucket = 1 + int(self.latency[i]/step)
            while len(hist) < bucket + 1:
                hist.append(0)
            hist[bucket] += 1
        return hist

    def sim_9002_rtt(self):
        t = []
        t_pto = []
        latest_rtt = self.latency[0]
        send_time = self.stamp[0]
        recv_time = send_time + latest_rtt
        smoothed_rtt = latest_rtt
        min_rtt = latest_rtt
        rttvar = latest_rtt/2
        pto = smoothed_rtt + 4*rttvar
        pto_time = send_time + pto
        t.append([ send_time, recv_time, min_rtt, smoothed_rtt, rttvar, pto ])
        for i in range(1,len(self.latency)):
            latest_rtt = self.latency[i]
            send_time = self.stamp[i]
            recv_time = send_time + latest_rtt
            if pto < latest_rtt:
                t_pto.append([ pto_time, pto ])
            if min_rtt > latest_rtt:
                min_rtt = latest_rtt
            smoothed_rtt = (7 * smoothed_rtt + latest_rtt)/8
            rttvar_sample = abs(smoothed_rtt - latest_rtt)
            rttvar = (3 * rttvar + rttvar_sample)/4
            pto = smoothed_rtt + 4*rttvar
            pto_time = send_time + pto
            t.append([ send_time, recv_time, min_rtt, smoothed_rtt, rttvar, pto ])
        df = pd.DataFrame(t, columns=["send_time", "recv_time", "min_rtt", "smoothed_rtt", "rttvar", "pto" ])
        df_pto = pd.DataFrame(t_pto, columns=["pto_time", "pto" ])
        return df, df_pto

class wifi_trace:
    def __init__(self):
        self.trace = []
        self.filtered = 0
        self.min_rtt = 99999999
        self.max_rtt = 0
        self.repeated = 0
        self.lost = 0
        self.alpha = 0
        self.pareto_likely = 0

    def load_one(self, x, filtering):
        ev_index = len(self.trace)
        if ('sent' in x) and ('down_t' in x) and ('echo' in x):
            ev = trace_event(x['number'], x['sent'], x['received'],  x['echo'], x['rtt'], x['up_t'], x['down_t'], x['phase'])
            if ev_index == 0 or (not filtering) or ev.echo > (self.trace[ev_index -1].echo + 1000):
                self.trace.append(ev)
            else:
                self.filtered += 1
        else:
            print("Cannot parse event " + str(ev_index))
            print("keys: " + str(x.keys()))
        return True

    def load(self, df, filtering):
        df.apply(lambda x: self.load_one(x, filtering), axis=1)

    # The octoping program approximates the value of the phase during the capture, 
    # but if we have the whole trace we can do better. We have the equations:
    #     up_t = (received - phase) - sent_at;
    #     down_t = rtt - up_t;
    # We compute the value of the phase that gives us
    #     min(up_t) ~= min(down_t)

    def get_min_up_down(self, phase):
        min_up = 99999999
        min_down = 99999999
        for ev in self.trace:
            if ev.rtt > 0:
                up_t = ev.received  - phase - ev.sent
                if up_t < min_up:
                    min_up = up_t
                down_t = ev.rtt - up_t
                if down_t < min_down:
                    min_down = down_t
        return min_up, min_down

    def tune_phase(self):
        # we try to balance so min(up_t) == min(down_t)
        phase = self.trace[0].phase
        min_up, min_down = self.get_min_up_down(phase)
        print("Before correction, phase: " + str(phase) + ", min_up: " + str(min_up) + ", min_down: " + str(min_down))
        phase += int((min_up - min_down)/2)
        min_up, min_down = self.get_min_up_down(phase)
        print("After correction, phase: " + str(phase) + ", min_up: " + str(min_up) + ", min_down: " + str(min_down))
        if min_up > 0 and min_down > 0:
            for ev in self.trace:
                ev.reset_phase(phase)
        else:
            print("Not resetting the phase.")

    def load_file(self, file_name, filtering=False):
        df = pd.read_csv(file_name, skipinitialspace=True)
        print("Loaded " + str(df.shape[0]) + " events from " + file_name)
        self.load(df, filtering)
        print("Loaded " + str(len(self.trace)) + " events, filtered " + str(self.filtered))

    def get_up_down(self, base_name):
        s_up = time_series(base_name + "_up")
        s_down = time_series(base_name + "_down")
        for ev in self.trace:
            if ev.rtt > 0:
                s_up.append(ev.up_t, ev.sent)
                s_down.append(ev.down_t, ev.received - ev.phase)
        return s_up, s_down

    def get_rtt_series(self, base_name):
        s_rtt = time_series(base_name + "_rtt")
        for ev in self.trace:
            if ev.rtt > 0:
                s_rtt.append(ev.rtt, ev.sent)
        return s_rtt

class wifi_simulator:
    def __init__(self, base_delay, drop_rates, limit):
        self.drop_rates = drop_rates
        self.base_delay = base_delay
        self.limit = limit
        self.trace = []
        while(len(self.drop_rates) < limit):
            self.drop_rates.append(0)

    def model_draw(self):
        delay = self.base_delay
        next_timer = 2*delay
        nb_repeat = 0
        last_timer = delay
        while nb_repeat < self.limit:
            r = random.uniform(0,1.0)
            if r < self.drop_rates[nb_repeat]:
                last_timer = next_timer
                delay += next_timer
                nb_repeat += 1
                next_timer *= 2
            else:
                break
        if nb_repeat == 0:
            delay -= int(random.uniform(0,last_timer))
        elif nb_repeat < 7:
            delay -= int(random.uniform(0,last_timer))
        return delay

    def get_trace(self, name, interval, nb_packets, max_delay):
        trace = time_series(name)
        send_time = 0
        recv_time = 0
        for i in range(0, nb_packets):
            delay = self.model_draw()
            received = send_time + delay
            if received > recv_time:
                recv_time = received
            else:
                delay = recv_time - send_time
            trace.append(delay, send_time)
            send_time += interval
        return trace

           
class trace_frequency:
    def __init__(self):
        self.rtt = []
        self.bucket = []
    def add_series(self, ser):
        self.rtt += ser.latency
        #for rtt in ser.latency:
        #    self.rtt.append(rtt)
    def add_trace(self, tra):
        for x in self.tra.trace:
            self.rtt.append(x.rtt)

    def compute_frequencies(self):
        self.bucket = []
        self.rtt.sort()
        limit = 2500
        l_name = "< " + str(limit)
        bsum = 0
        overflow=150000
        total = len(self.rtt)
        for rtt in self.rtt:
            while rtt > limit:
                frq = bsum/total
                if bsum == 0:
                    l_count = 0
                else:
                    l_count = math.log10(bsum)
                self.bucket.append([ l_name, bsum, frq, l_count])
                bsum = 0
                if rtt > overflow:
                    l_name = "> " + str(overflow)
                    limit = 1000000000
                else:
                    l_name = str(limit) + "--"
                    limit += 2500
                    l_name += str(limit)
            bsum += 1

        if bsum > 0:
            frq = bsum/total
            l_count = math.log10(bsum)
            self.bucket.append([ l_name, bsum, frq, l_count ])

        df = pd.DataFrame( self.bucket, columns=[ 'range', 'count', 'frq', 'l10_frq' ])
        return df

# main
# first, get the trace copy

out_dir = sys.argv[1]
file_names = sys.argv[2:]

#sim_far = [ "sim_far", [ 0.95, 0.25, 0.5, 0.625, 0.625, 0.05, 0 ]]
#sim_mid = [ "sim_mid", [ 0.95, 0.15, 0.25, 0.625, 0.625, 0.05, 0 ]]
#sim_near = [ "sim_near", [ 0.95, 0.03, 0.04, 0.625, 0.25, 0.05, 0 ]]
sim_far = [ "sim_far",  [1, 0.99, 0.25, 0.5, 0.625, 0.8, 0.08, 0, 0, 0 ]]
sim_mid = [ "sim_mid", [ 1, 0.6125, 0.42, 0.4, 0.45, 0.8, 0.05, 0, 0, 0 ]]
sim_near= [ "sim_near", [ 0.99, 0.3, 0.125, 0.04, 0.5, 0.25, 0, 0, 0, 0 ]]

sims = [ sim_far, sim_mid, sim_near ]

out_csv = os.path.join(out_dir, "drop_rates.csv" )
step_val = 800
sim_steps = 3000
sum_tf = trace_frequency()

with open(out_csv, "w") as F:
    F.write("series, N, min_t, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10\n")
    trace_list = []
    for file_name in file_names:
        base_name = os.path.basename(os.path.normpath(file_name))
        print("Processing " + base_name + " (" + file_name + ")")
        if base_name.endswith(".csv"):
            base_name = base_name[:-4]
        trace = wifi_trace()
        trace.load_file(file_name, filtering=True)
        s_rtt = trace.get_rtt_series(base_name)
        trace_list.append(s_rtt)
        sum_tf.add_series(s_rtt)
        for serie in [s_rtt ]:
            serie.series_stats()
            if serie.min_latency > 0:
                serie.pareto_eval()
                serie.model_match(F, step=step_val)
    
    for sim in sims:
        w_sim = wifi_simulator(step_val, sim[1], 8)
        s_sim = w_sim.get_trace(sim[0], 20000, 3000, 250000)
        trace_list.append(s_sim)
        F.write(sim[0] + "," + str(len(s_sim.latency)) + "," + str(step_val))
        i = 1
        for drop_rate in sim[1]:
            F.write(',' + str(drop_rate))
            i += 1
        while i <= 10:
            F.write(',0')
            i += 1
        F.write("\n")
        sim_file = os.path.join(out_dir, sim[0] + ".csv" )
        df = s_sim.get_df()
        df.to_csv(sim_file)

sum_csv = os.path.join(out_dir, "cumulative.csv" )
sum_df = sum_tf.compute_frequencies()
sum_df.to_csv(sum_csv)


for s_sim in trace_list:
    df = s_sim.get_df()
    image_file = os.path.join(out_dir, s_sim.name + ".png")
    axa = df.plot.scatter(x='recv_time', y='rtt', ylim = [ 0, 250000 ], marker='+', title=s_sim.name)
    plt.savefig(image_file)
    rtt_9002_file = os.path.join(out_dir, s_sim.name + "_9002.png")
    df_9002, df_pto = s_sim.sim_9002_rtt()
    #axa = df.plot.line(x='send_time', y='rtt', ylim = [ 0, 250000 ], title=s_sim.name)
    df_9002.plot.scatter(ax=axa, x="recv_time", y="min_rtt", marker='.', color='orange')
    #df_9002.plot.scatter(ax=axa, x="recv_time", y="smoothed_rtt", marker='.', alpha=0.25, color='green')
    df_pto.plot.scatter(ax=axa, x="pto_time", y="pto", alpha=1, color='red', legend='pto')
    #df_9002.plot.line(ax=axa, x="send_time", y="rttvar", alpha=0.5, color='orange')
    plt.savefig(rtt_9002_file)

hist_list = []
hist_step = 2500
hist_max = 0
for s_sim in trace_list:
    h = s_sim.get_hist(hist_step)
    hist_list.append(h)
    if len(h) > hist_max:
        hist_max = len(h)
headers = [ 'trace' ]
bucket = 0
for i in range(1,hist_max):
    bucket += hist_step
    headers.append(str(bucket))
for h in hist_list:
    print(h[0] + " len:" + str(len(h)) + " /" + str(len(headers)))
    while len(h) < hist_max:
        h.append(0)
df = pd.DataFrame(hist_list, columns=headers)
hist_file = os.path.join(out_dir, "histograms.csv")
df.to_csv(hist_file)








#sim = wifi_simulator(loss_rate, correlated_rate, base_delay, base_timer, limit)
#trace_df = sim.get_trace(20000, 3750, 0.95, 0.573, 24800, 200000)
#trace_df.to_csv(sys.argv[2])
#axa = trace_df.plot.scatter(x='sent', y='rtt', color='green')
#plt.show()

