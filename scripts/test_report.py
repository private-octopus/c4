# Write a test report from the list of results.
#
# The statistics directory (./tmp/stats) has one CSV file per test case. The CSV
# has column for:
#  - result: Success or Fail
#  - rep: from 1 to N, the repeat number
#  - time: the execution time of the test
#  - av_latency: for "media" time of tests, the average frame latency.
#  - max_latency: for "media" time of tests, the max frame latency.
#  - test: the name of the test
#
# we could do graphs, or compute key statistics like media, top 95% and max for
# the time, av_latency and max_latency. We can do that with panda, e.g.:
#   q75 = df['Column2'].quantile(q=0.75)
#   qmax = df['Column2'].max()
#
# The name of the file is normally the name of the test. That name is
# composed the for: <alg_name> "_" <test_case> ".csv". 
#
# We should group the test by test cases, and possibly the test cases by related
# groups like "single connection", "wi-fi", "Compete", etc. We may see results
# for c4, bbr and cubic. We want to present them in a comparison table.
# Maybe write all that in markdown.
#



import sys
import os
from pathlib import Path
import pandas as pd
import traceback

# dict of valid algorithms
algo_dict = {
    "c4":0,
    "bbr":1,
    "cubic":2 }

# groups of tests

test_groups = [
    [ "network events", ["alone", "alone_200", "alone_1_5M", "alone_512k", "low_and_up", "drop_and_back", "blackhole", "short_long", "satellite"], "time", "Reaction to network events" ],
    [ "compete", [ "vs_bbr", "vs_c4", "vs_cubic", "after_c4", "before_c4", "vs_c4_lg", "vs_c4_lg2", "vs_bbr_lg", "vs_bbr_lg2", "vs_cubic_lg", "vs_cubic_lg2"], "time", "Competition" ],
    [ "wifi", [ "wifi_bad", "wifi_fade", "wifi_suspension", "wifi_bad_bbr", "wifi_bad_c4", "wifi_bad_cubic" ], "time", "Wi-Fi"],
    [ "ecn", [ "ecn", "ecn_c4", "ecn_cubic", "ecn_bbr" ], "time", "ECN" ],
    [ "media", [ "media", "media10", "media_600fr", "media_short_long", "media_wb", "media_wf", "media_ws", "media_ecn" ], "media", "Media" ],
]


# Operations on a test report.
class test_report:
    def __init__(self, test_case, algo):
        self.df = None
        self.test_case = test_case
        self.algo = algo

    def load(self, file_name):
        self.df = pd.read_csv(file_name)
    
    def average(self, metric, scale=1):
        x = self.df[metric].mean() * scale
        return int(round(x))

    def top90(self, metric, scale=1):
        x = self.df[metric].quantile(q=0.9, interpolation='linear') * scale
        return int(round(x))

    def top90_combo(self, metric_a, metric_b):
        x = (self.df[metric_a] + self.df[metric_b]).quantile(q=0.9, interpolation='linear')
        return int(round(x))

class test_case_group:
    def __init__(self, tc, nb_alts):
        self.tc = tc
        self.alg_report = [ None, None, None ]
        self.q_alg_report = [ None, None, None ]
        for i in range(0,nb_alts):
            self.alg_report.append(None)
            self.q_alg_report.append(None)

# Grouping of reports

class report_list:
    def __init__(self, nb_alts):
        self.test_cases=dict()
        self.reported = set()
        self.nb_alts = nb_alts

    def add_dir(self, dir_path, n):
        for report_name in os.listdir(dir_path):
            if report_name.endswith(".csv"):
                is_q = report_name.startswith("q_")
                base_name = report_name[2:] if is_q else report_name
                algo_case = base_name[:-4]
                algo_case_part = algo_case.split("_")
                if len(algo_case_part) > 1:
                    algo = algo_case_part[0]
                    if algo in algo_dict and (n == 0 or algo == "c4"):
                        tc = algo_case[(len(algo) + 1):]
                        rp = test_report(algo, tc)
                        rp.load(os.path.join(dir_path, report_name))
                        if not tc in self.test_cases:
                            self.test_cases[tc] = test_case_group(tc, self.nb_alts)
                        if n == 0:
                            algo_rank = algo_dict[algo]
                        else:
                            algo_rank = n+2
                        if is_q:
                            self.test_cases[tc].q_alg_report[algo_rank] = rp
                        else:
                            self.test_cases[tc].alg_report[algo_rank] = rp


    def do_metric_report(self, F, grp, tl, title, report_attr, value_fn):
        top = " " + title + " for " + grp + " tests"

        F.write("### " + top + "\n\n")
        F.write("| " + top + "| c4 | bbr | cubic |")
        for i in range(0, self.nb_alts):
            F.write(" c4_" + str(i+1) + " |")
        F.write("\n")

        F.write("| --------- | ---:| ---:| ---:|")
        for i in range(0, self.nb_alts):
            F.write(" ---:|")
        F.write("\n")

        for tc in tl:
            if not tc in self.test_cases:
                continue
            tc_data = self.test_cases[tc]
            reports = getattr(tc_data, report_attr)

            sm = "| " + tc + " | "
            has_metric = False
            for i in range(0,3 + self.nb_alts):
                x = 0
                if reports[i] == None:
                    sm += " |"
                else:
                    x = value_fn(reports[i])
                    sm += " " + str(x) + " |"
                    if x > 0:
                        has_metric = True
            if has_metric:
                F.write(sm + '\n')
                self.reported.add(tc)

        F.write("\n")

    def do_case_metrics(self, F, grp, tl, metric_type):
        if metric_type == 'time':
            self.do_metric_report(F, grp, tl, "average time", 'alg_report', lambda rp: rp.average('time'))
            self.do_metric_report(F, grp, tl, "top 90% time", 'alg_report', lambda rp: rp.top90('time'))
            self.do_metric_report(F, grp, tl, "average RTT", 'q_alg_report', lambda rp: rp.average('ave_rtt'))
            self.do_metric_report(F, grp, tl, "top 90% of RTT + standard deviation", 'q_alg_report', lambda rp: rp.top90_combo('ave_rtt', 'std_rtt'))
            if grp in ('compete', 'wifi'):
                self.do_metric_report(F, grp, tl, "average load (%)", 'q_alg_report', lambda rp: rp.average('load', 100))
                self.do_metric_report(F, grp, tl, "top 90% load (%)", 'q_alg_report', lambda rp: rp.top90('load', 100))
        else:
            self.do_metric_report(F, grp, tl, "average av_latency", 'alg_report', lambda rp: rp.average('av_latency'))
            self.do_metric_report(F, grp, tl, "top 90% max_latency", 'alg_report', lambda rp: rp.top90('max_latency'))

    def do_report(self, F):
        self.reported = set()
        F.write("# Statistics\n")
        F.write("Here is a collection of statistics on all test cases.\n\n")
        for tg in test_groups:
            F.write("## " + tg[3] + "\n\n")
            F.write("Here the statistics for the " + tg[0] + " test cases.\n\n")
            self.do_case_metrics(F, tg[0], tg[1], tg[2])
            F.write("\n")
         
        if len(self.test_cases) > len(self.reported):
            F.write("## others\n")
            F.write("Here the statistics for the other test cases.\n\n")
            F.write("| average time | c4 | bbr | cubic |\n")
            F.write("|----| ---:| ---:| ---:|\n")
            for tc in self.test_cases:
               if not tc in self.reported:
                    sm = "| " + tc + " | " 
                    has_metric = False
                    for i in range(0,3):
                        x = 0
                        if self.test_cases[tc].alg_report[i] == None:
                            sm += " |"
                        else:
                            x = self.test_cases[tc].alg_report[i].average('time')
                            sm += " " + str(x) + " |"
                        if x > 0:
                            has_metric = True
                    if has_metric:
                        F.write(sm + '\n')

            F.write("\n")

def usage(ret_code):
    print("Usage: " + sys.argv[0] + " <output.md> <stats-dir> [<alt-dir>*]\n") 
    print("Stats dir contains stats summary for tests with C4 (current), BBR and Cubic.")
    print("Each optional alt dir contains resuls with a version of C4 in development.")
    print("Results lines will show results for C4, BBR, Cubic and each version in test.")
    if ret_code != 0:
        exit(ret_code)

# main

if len(sys.argv) < 3:
    usage(-1)

rl = report_list(len(sys.argv) - 3)

for i in range(2, len(sys.argv)):
    rl.add_dir(sys.argv[i], i-2)

with open(sys.argv[1], "wt") as F:
    rl.do_report(F)
