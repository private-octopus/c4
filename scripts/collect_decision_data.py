# Collecting logs of collection data
#
# The idea is to run series of simulations to check whether C4 is making the "right"
# decision. We are concerned with two specific decisions:
#
# - if RTT is higher than current max, detect congestion or increase max.
# - if queues are apparently building, detect creeping congestion.
#
# In the code, these decisions will be based on criteria, i.e., features
# when considering machine learning. We will probably need to iterate and
# find specific features. Candidate features include checking whether queues
# are building up, comparing delays to min, average or max, comparing
# delay changes to rtt-var, looking at past congestion events, growth
# events, recovery, previous alpha, current alpha.
#
# Decisions will logically be made ar three specific decision points:
#
# - upon observation of a high RTT.
# - at the end of an epoch.
# - just before entering pushing.
#
# In as much as possible, we will compare the decisions to a "ground truth",
# such as the plausible data rate, or the plausible max RTT. These will
# normally be a parameter of the simulation. Some simulations involve
# changing path characteristics, and we will need to update the "ground
# truth" accordingly. Other simulations may involve computing with C4,
# Cubic or BBR, and again that will imply changes in "ground truth" over
# time.
#
# The collection of data has three components:
#
# - instrumenting C4 to document decisions and value of features at time of decisions,
# - running a simulation with specific conditions and prediction of "ground truth"
# - retrieve the data from the qlog in order to build "log tables".
# 
# Then, we will try to infer the features or mixes of features that can
# help in the decision making.

import sys
import os
import pandas as pd
import random

# create a simulation, which can be one of:
# - single connection or compete (single for initial test)
# - if compete, decide whether background starts first or last
# - wifi or static jitter.
#
# This gets the following parameters:
#
# nb_connections: "1" in single scenarios (current test) or "2" if competing.
# main_cc_algo: always "c4"
# main_start_time: 0 if "main first", "duration/4" if "main last"
# background_cc_algo: c4, cubic or bbr. Only if competing.
# background_start_time: "duration/4" if "main first", 0 if "main last"
# main_scenario_text: b1:*1:397:<filesize>; Need to cover the target duration. At least 
# background_scenario_text: =b1:*1:397:<2*filesize>; 
# main_target_time: Computed based on delay, bandwidth, and file size. Include competition.
# data_rate_in_gbps: Random between 0.01 and 1.0. Pick on log scale.
# latency: Could be "1000" in "wi-fi" scenarios, random between "1000" and "100000" in others
# jitter: Between 1000 and 10000 if wifi. No jitter if straight.
# wifi_jitter: "1" in wi-fi 
# queue_delay_max: set long enough to match 2*latency plus max jitter plus some margin
# icid: 32 bit random number chosen for this simulation, in hex.
# qlog_dir: cclog
#
# Ground truth is expressed as a table, with start time, gt bandwidth, gt RTT

class ground_truth_line:
    def __init__(self, start_time, bandwidth, bandwidth_share, rtt_max):
        self.start_time = start_time
        self.bandwidth = bandwidth
        self.bandwidth_share = bandwidth_share
        self.rtt_max = rtt_max
    def text(self):
        s = "start: " + str(self.start_time) + ", "
        s += "gbps: " + str(self.bandwidth) + ", "
        s += "gbps_share: " + str(self.bandwidth_share) + ", "
        s += "rtt_max: " + str(self.rtt_max)
        return s
    def to_row(self):
        r = [
           self.start_time,
           self.bandwidth,
           self.bandwidth_share,
           self.rtt_max ]
        return r
    def columns():
        return [
            'start_time',
            'bandwidth',
            'bandwidth_share',
            'rtt_max' ]

def get_rtt_max(latency, jitter, is_wifi):
    if is_wifi:
        rtt_max = 2*latency + 250000
    else:
        rtt_max = 2*(latency+jitter)
    return rtt_max

def find_picoquic(script_file):
    script_path = os.path.abspath(script_file)
    #print(script_path)
    solution_path = os.path.dirname(script_path)
    #print(solution_path)
    parent = os.path.dirname(solution_path)
    #print(parent)
    one_up = os.path.dirname(parent)
    #print(one_up)
    picoquic_path = os.path.join(one_up, "picoquic")
    return picoquic_path

def create_simulation(test_dir, sim_file_path, icid_v, is_wifi, gbps, latency, jitter, bg_algo, is_first):
    if check_dirs(test_dir) != 0:
        return -1
    rtt_max = get_rtt_max(latency, jitter, is_wifi)
    start_time = 0
    nb_connections = 1
    file_size = (int)(gbps*125000000*4)
    if file_size > 50000000:
        # limit so that simulations do not last too long
        file_size = 50000000
    main_scenario_text= "=b1:*1:397:" + str(file_size) + ";"
    target_time = 5000000
    background_start_time = 0
    queue_delay_max = rtt_max + 30000
    if bg_algo != "":
        nb_connections = 2
        if not is_first:
            start_time = 1000000
        background_start_time = 1000000 - start_time
        target_time = 2*target_time + start_time
    ground_truth = []
    if nb_connections == 1:
        ground_truth.append(ground_truth_line(0, gbps, gbps, rtt_max))
    elif is_first:
        ground_truth.append(ground_truth_line(0, gbps, gbps, rtt_max))
        ground_truth.append(ground_truth_line(background_start_time, gbps, gbps/nb_connections, rtt_max))
    else:
        ground_truth.append(ground_truth_line(0, gbps, gbps/2, rtt_max))

    qlog_path = os.path.join(test_dir, "qlog")

    with open(sim_file_path,"w") as F:
        F.write("nb_connections: " + str(nb_connections) + "\n")
        F.write("data_rate_in_gbps: " + str(gbps) + "\n")
        F.write("latency: " + str(latency) + "\n")
        F.write("jitter: " + str(latency) + "\n")
        if is_wifi:
            F.write("wifi_jitter: 1\n")
        F.write("main_cc_algo: c4\n")
        F.write("main_start_time: " + str(start_time) + "\n")
        F.write("main_scenario_text: =b1:*1:397:" + str(file_size) + ";\n")
        F.write("main_target_time: " + str(target_time) + "\n")
        F.write("queue_delay_max: " + str(queue_delay_max) + "\n")
        
        if bg_algo != "":
            F.write("background_cc_algo: " + bg_algo + "\n")
            F.write("background_start_time: " + str(background_start_time) + "\n")
            F.write("background_scenario_text: =b2:*1:397:" + str(2*file_size) + ";\n")

        F.write("icid: " + icid_v + "\n")
        F.write("qlog_dir: " + qlog_path + "\n")

    return ground_truth

# extract statistics from the qlog file produced by the simulation.
def extract_stat_lines(qlog_file_name):
    header_set = [ 'time', 'rate', 'n-rate', 'bytes', 'rtt', 'send-delay', 'nominal-rtt', 'state', 'path-bw',
                 'is_cc', 'beta', 'cc', 'ecn_a', 'smoothed_rtt', 'rtt_variant', 'alpha_previous' ]
    line_set = []

    for line in open(qlog_file_name, "r"):
        if "C4_rate" in line:
            line = line.strip()
            if line.startswith('['):
                line = line[1:]
            if line.endswith('"}],'):
                line = line[:-4]
            parts = line.split(',')

            r = []
            r.append(int(parts[0]))
            for i in range(4, len(parts)):
                r.append(int(parts[i]))
            if len(r) < len(header_set):
                print(line)
                exit(-1)
            line_set.append(r)
    df = pd.DataFrame(line_set, columns=header_set)
    return df

# decorate the statistics with the ground truth

def add_ground_truth_line(t, x):
    gt = ground_truth_line(x['start_time'], x['bandwidth'], x['bandwidth_share'], x['rtt_max'])
    t.append(gt)

def get_ground_truth_table(ground_truth_csv):
    gt_df = pd.read_csv(ground_truth_csv)
    ground_truth = []
    gt_df.apply(lambda x: add_ground_truth_line(ground_truth, x), axis=1)
    return ground_truth

def get_ground_truth(x, ground_truth):
    event_time = x['time']
    rtt = 0
    bw = 0
    gtx = ground_truth[0]
    for gt in ground_truth:
        if gt.start_time > event_time:
            break
        gtx = gt
    return gtx

def get_ground_truth_rtt_max(x, ground_truth):
    return get_ground_truth(x, ground_truth).rtt_max

def get_ground_truth_bandwidth(x, ground_truth):
    return (int)(get_ground_truth(x, ground_truth).bandwidth*125000000)

def get_ground_truth_bandwidth_share(x, ground_truth):
    return (int)(get_ground_truth(x, ground_truth).bandwidth_share*125000000)

def annotate_stat_lines(df, ground_truth):
    df['gt_rtt_max'] = df.apply(lambda x: get_ground_truth_rtt_max(x, ground_truth), axis=1)
    df['gt_bandwidth'] = df.apply(lambda x: get_ground_truth_bandwidth(x, ground_truth), axis=1)
    df['gt_bandwidth_share'] = df.apply(lambda x: get_ground_truth_bandwidth_share(x, ground_truth), axis=1)
    return df

def run_simulation(script_file, test_path, exe_dir):
    exe_name = "pico_sim"
    if os.name == "nt":
        exe_name += ".exe"
    exe_path = os.path.join(exe_dir, exe_name)
    log_path = os.path.join(os.path.dirname(exe_path),"cclog")
    if not os.path.isfile(exe_path):
        print (exe_path + " is not a file!")
        exit(-1)
    elif not os.path.isdir(log_path):
        print("Not a directory: " + log_path)
        exit(-1)
    if not os.path.isfile(test_path):
        print (test_path + " is not a file!")
        ret = -1;
    else:
        cmd = exe_path + " -S " + find_picoquic(script_file) + " " + test_path
        #print("Running: " + cmd)
        ret = os.system(cmd)
    return ret

def gt_save(ground_truth, gt_file_path):
    t = []
    for gt in ground_truth:
        t.append(gt.to_row())
    df = pd.DataFrame(t, columns=ground_truth_line.columns())
    df.to_csv(gt_file_path)

def run_one(script_file, test_dir, exe_dir, icid, is_wifi, gbps, latency, jitter, bg_algo, is_first):
    # First check that the directory is valid.
    ret = check_dirs(test_dir)
    if ret != 0:
        return ret

    sim_file_name = "test_" + icid + ".txt"
    sim_file_dir = os.path.join(test_dir, "sim_spec")
    sim_file_path = os.path.join(sim_file_dir, sim_file_name)
    ground_truth = create_simulation(test_dir, sim_file_path, icid, is_wifi, gbps, latency, jitter, bg_algo, is_first)
    qlog_path = os.path.join(test_dir, "qlog")
    qlog_file_name = icid + "00000000.server.qlog"
    qlog_file_path = os.path.join(qlog_path, qlog_file_name)
    ret = run_simulation(script_file, sim_file_path, exe_dir)
    if ret != 0:
        print("simulation of " + sim_file_path + " returns: " + str(ret))
        ret = 0
    df = extract_stat_lines(qlog_file_path)
    if df.shape[0] <= 0:
        print("For " + sim_file_path + ", no event found in " + qlog_file_path)
        ret = -1
    else:
        gt_dir = os.path.join(test_dir, "gt")
        gt_file_name = "gt_" + icid + ".csv"
        gt_file_path = os.path.join(gt_dir, gt_file_name)
        gt_save(ground_truth, gt_file_path)
        df_an = annotate_stat_lines(df, ground_truth)
        stat_file_name = "stats_" + icid + ".csv"
        stat_file_dir = os.path.join(test_dir, "dec")
        stat_file_path = os.path.join(stat_file_dir, stat_file_name)
        df_an.to_csv(stat_file_path)

    return ret

def rnd_params(seed):
    compete_range = [ "", "cubic", "bbr", "c4" ]
    # is_wifi
    is_wifi = (seed&1) != 0
    seed >>= 1
    # gbps, latency, jitter, bg_algo, is_first
    gbps_range = [ 0.01, 0.02, 0.05, 0.1 ]
    gbps = gbps_range[ seed%len(gbps_range) ]
    seed = int(seed/len(gbps_range));
    # latency
    latency_range = [ 1000, 2000, 5000, 10000, 20000, 50000 ]
    latency = latency_range[ seed%len(latency_range) ]
    seed = int(seed/len(latency_range));
    # jitter
    jitter_range = [ 0, 1000, 2000, 3000, 4000, 5000, 6000 ]
    jitter = jitter_range[ seed%len(jitter_range) ]
    seed = int(seed/len(jitter_range));
    # bg_algo
    compete_range = [ "", "cubic", "bbr", "c4" ]
    bg_algo = compete_range[ seed%len(compete_range) ]
    seed = int(seed/len(compete_range));
    # is_first
    is_first = False

    return is_wifi, gbps, latency, jitter, bg_algo, is_first

def rnd_gen():
    # Each test is identified by a 32 bit random icid.
    # the simulation parameters are derived from the 32 bit value
    # todo: check that the value is unique

    icid_n = random.getrandbits(32)
    icid = "{0:08x}".format(icid_n)
    is_wifi, gbps, latency, jitter, bg_algo, is_first = rnd_params(icid_n)
    
    return icid, is_wifi, gbps, latency, jitter, bg_algo, is_first

def check_dirs(test_dir):
    ret = 0
    if not os.path.isdir(test_dir):
        print("Not a directory: " + test_dir)
        ret = -1
    else:
        for sub_dir in [ 'sim_spec', 'gt', 'qlog', 'dec' ]:
            x_dir = os.path.join(test_dir, sub_dir)
            if not os.path.isdir(x_dir):
                os.mkdir(x_dir)
                print("Created: " + x_dir)
    return ret

def rnd_run(script_file, test_dir, exe_dir):
    ret = check_dirs(test_dir)
    if ret == 0:
        icid, is_wifi, gbps, latency, jitter, bg_algo, is_first = rnd_gen()
        ret = run_one(script_file, test_dir, exe_dir, icid, is_wifi, gbps, latency, jitter, bg_algo, is_first)
    return ret

# extract the records showing data rate in excess of nominal rate

def add_td_row_to_table(x, t, columns, is_expected):
    r = []
    for c in columns:
        r.append(x[c])
    v_expected = 0
    if is_expected:
        v_expected = 1
    r.append(v_expected)
    t.append(r)

def check_bw_increase(x, t, columns):
    if x['rate'] > x['n-rate']:
        is_too_high = (x['rate'] > x['gt_bandwidth'])
        is_expected = (x['rate'] < x['gt_bandwidth_share'])
        if is_too_high or is_expected:
            add_td_row_to_table(x, t, columns, is_expected)

def check_rtt_increase(x, t, columns):
    if x['rtt'] > x['nominal-rtt'] and x['state'] > 1 and x['nominal-rtt'] < x['gt_rtt_max']:
        is_expected = (x['rtt'] < x['gt_rtt_max'])
        add_td_row_to_table(x, t, columns, is_expected)
        
def check_rtt_initial(x, t, columns):
    if x['rtt'] > x['nominal-rtt'] and x['state'] == 1 and x['nominal-rtt'] < x['gt_rtt_max']:
        is_expected = (x['rtt'] < x['gt_rtt_max'])
        add_td_row_to_table(x, t, columns, is_expected)

def check_td_row(file_dir, function_name):
    # get the list of files
    files = [file for file in os.listdir(file_dir) if not file.startswith('.')]
    # load of the files in tables
    t = []
    columns = []
    for file in files:
        file_path = os.path.join(file_dir, file)
        df = pd.read_csv(file_path)
        if len(columns) == 0:
            columns = df.columns.values.tolist()
            columns = columns[1:]
        df.apply(lambda x: function_name(x, t, columns), axis=1)
    columns.append('is_expected')
    r_df = pd.DataFrame(t, columns=columns)
    return r_df

# main

if sys.argv[1] == "test-ext":
    df = extract_stat_lines(sys.argv[2])
    if df.shape[0] > 1:
        df.to_csv(sys.argv[3])
        print("Saved " + str(df.shape[0]) + " lines to " + sys.argv[3])
elif sys.argv[1] == "test-gen":
    test_dir = sys.argv[2]
    sim_file_path = sys.argv[3]
    ground_truth = create_simulation(test_dir, sim_file_path, "12340000", True, 0.02, 1500, 6000, "c4", True)
    t = []
    for gt in ground_truth:
        t.append(gt.to_row())
    df = pd.DataFrame(t, columns=ground_truth_line.columns())
    df.to_csv(sys.argv[4])
elif sys.argv[1] == "test-run":
    ret = run_simulation(sys.argv[0], sys.argv[2], sys.argv[3])
    print("simulation returns: " + str(ret))
elif sys.argv[1] == "test-dec":
    ground_truth = get_ground_truth_table(sys.argv[3])
    df = extract_stat_lines(sys.argv[2])
    df_an = annotate_stat_lines(df, ground_truth)
    df_an.to_csv(sys.argv[4])
elif sys.argv[1] == "test-one":
    test_dir = sys.argv[2]
    exe_dir = sys.argv[3]
    icid = "23450000"
    ret = run_one(sys.argv[0], test_dir, exe_dir, icid, True, 0.05, 2000, 5000, "", False)
    print("test one returns " + str(ret))
elif sys.argv[1] == "test-rnd":
    print("icid,is_wifi,gbps,latency,jitter,bg_algo,is_first")
    for i in range(0,16):
        icid, is_wifi, gbps, latency, jitter, bg_algo, is_first = rnd_gen()
        print (icid + ", " + 
               str(is_wifi) + ", " +
               str(gbps) + ", " +
               str(latency) + ", " +
               str(jitter) + ", " +
               "\"" + bg_algo + "\", " +
               str(is_first))
elif sys.argv[1] == "run":
    test_dir = sys.argv[2]
    exe_dir = sys.argv[3]
    for i in range(0,16):
        ret = rnd_run(sys.argv[0], test_dir, exe_dir)
        if ret != 0:
            print ("error at test #" + str(i))
elif sys.argv[1] == "check-bw":
    test_dec_dir = sys.argv[2]
    test_df = check_td_row(test_dec_dir, check_bw_increase)
    test_df.to_csv(sys.argv[3])
elif sys.argv[1] == "check-rtt":
    test_dec_dir = sys.argv[2]
    test_df = check_td_row(test_dec_dir, check_rtt_increase)
    test_df.to_csv(sys.argv[3])
elif sys.argv[1] == "check-rtt-initial":
    test_dec_dir = sys.argv[2]
    test_df = check_td_row(test_dec_dir, check_rtt_initial)
    test_df.to_csv(sys.argv[3])
else:
    print("Sorry, not ready yet.")
