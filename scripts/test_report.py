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
    [ "simple", ["alone", "alone_200", "low_and_up", "drop_and_back", "blackhole", "short_long", "satellite"], "time"],
    [ "media", [ "media", "media10", "media_600fr", "media_short_long", "media_wb", "media_wf", "media_ws" ], "media" ],
    [ "compete", [ "vs_bbr", "vs_c4", "vs_cubic", "after_c4", "before_c4", "vs_c4_lg", "vs_c4_lg2", "vs_bbr_lg", "vs_bbr_lg2", "vs_cubic_lg", "vs_cubic_lg2"], "time" ],
    [ "wifi", [ "wifi_bad", "wifi_fade", "wifi_suspension", "wifi_bad_bbr", "wifi_bad_c4", "wifi_bad_cubic" ], "time" ],
    [ "ecn", [ "ecn", "ecn_c4", "ecn_cubic", "ecn_bbr" ], "time" ]
]


# Operations on a test report.
class test_report:
    def __init__(self, test_case, algo):
        self.df = None
        self.test_case = test_case
        self.algo = algo


    def load(self, file_name):
        self.df = pd.read_csv(file_name)
    
    def average(self, metric):
        x = self.df[metric].mean()
        return int(round(x))
        
    def top90(self, metric):
        x = self.df[metric].quantile(q=0.9, interpolation='linear')
        return int(round(x))

class test_case_group:
    def __init__(self, tc):
        self.tc = tc
        self.alg_report = [ None, None, None ]

# Grouping of reports

class report_list:
    def __init__(self):
        self.test_cases=dict()
        self.reported = set()

    def add_dir(self, dir_path):
        for report_name in os.listdir(dir_path):
            if report_name.endswith(".csv"):
                algo_case = report_name[:-4]
                algo_case_part = algo_case.split("_")
                if len(algo_case_part) > 1:
                    algo = algo_case_part[0]
                    if algo in algo_dict:
                        tc = algo_case[(len(algo) + 1):]
                        rp = test_report(algo, tc)
                        rp.load(os.path.join(dir_path, report_name))
                        if not tc in self.test_cases:
                            self.test_cases[tc] = test_case_group(tc)
                        self.test_cases[tc].alg_report[algo_dict[algo]] = rp

    

    def do_metric_report(self, F, grp, tl, metric, use_top_90):
        if use_top_90:
            top = " top 90% " + metric
        else:
            top = " average " + metric
        top += " for " + grp + " tests"

        F.write("### " + top + "\n")
        F.write("| " + top + "| c4 | bbr | cubic |\n")
        F.write("| --------- | ---:| ---:| ---:|\n")

        for tc in tl:
            if not tc in self.test_cases:
                continue
            tc_data = self.test_cases[tc]

            sm = "| " + tc + " | " 
            has_metric = False
            for i in range(0,3):
                x = 0
                if tc_data.alg_report[i] == None:
                    sm += " |"
                else:
                    if use_top_90:
                        x = tc_data.alg_report[i].top90(metric)
                    else:
                        x = tc_data.alg_report[i].average(metric)
                    sm += " " + str(x) + " |"
                    if x > 0:
                        has_metric = True
            if has_metric:
                F.write(sm + '\n')
                self.reported.add(tc)

    def do_case_metrics(self, F, grp, tl, metric_type):
        if metric_type == 'time':
            self.do_metric_report(F, grp, tl,'time', False)
            self.do_metric_report(F, grp, tl, 'time', True)
        else:
            self.do_metric_report(F, grp, tl, 'av_latency', False)
            self.do_metric_report(F, grp, tl, 'max_latency', True)

    def do_report(self, F):
        self.reported = set()
        F.write("# Statistics\n")
        F.write("Here is a collection of statistics on all test cases.\n\n")
        for tg in test_groups:
            F.write("## " + tg[0] + "\n")
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

# main

rl = report_list()

rl.add_dir(sys.argv[1])

with open(sys.argv[2], "wt") as F:
    rl.do_report(F)
