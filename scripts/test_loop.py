# Script for testing the C4/C4R code.
#
# For each test case starting with C4 (or C4R) in the sim_case folder,
# run the test up to 100 times, or until it fails. For each trial,
# get a log in the cclog repository.
#
# arguments: test_loop.pt [path_of_the_exe]


import os
import sys
import concurrent.futures
import time
from pathlib import Path


def run_one_test(t_name, sim_path, exe_path, exe_options, bucket_id, stats_dir):
    ret = 0
    test_path = os.path.join(sim_path, t_name)
    t_file_name = os.path.basename(test_path)
    t_id_parts = t_file_name.split(".")
    t_id = t_id_parts[0]
    stats_path = os.path.join(stats_dir, t_id + ".csv")
    ##print("Stats path for " + t_id + ": " + stats_path)
    if not os.path.isfile(test_path):
        print (str(bucket_id) + ": " + test_path + " is not a file!")
        ret = -1;
    else:
        range_max = 100
        if t_name == "c4_satellite.txt":
            range_max = 20
        cmd = exe_path + " -N " + str(range_max) + " " + " ".join(exe_options) + " " + test_path
        #print(cmd)
        try:
            has_fail = False
            filestream = os.popen(cmd)
            res = filestream.read()
            #print("returned: " + str(len(res.split('\n'))) + " lines.")
            with open(stats_path, "wt") as stats:
                stats.write("result,rep,time,av_latency,max_latency,test\n")
                for l in res.split('\n'):
                    if l.startswith("Success,") or l.startswith("Fail,"):
                        stats.write(l + '\n')
                    if l.startswith("Fail,"):
                        print(l)
        except Exception as exc:
             print('Command ' + cmd + ' generated an exception: %s' % (exc))
             ret = -1
    return ret

def run_one_list(tb):
    ret = 0
    for t_name in tb.test_list:
        if run_one_test(t_name, tb.sim_path, tb.exe_path, tb.exe_options, tb.id, tb.stats_dir) != 0:
            ret = -1
    return ret

class test_bucket:
    def __init__(self, bucket_id, test_list, sim_path, exe_path, exe_options, stats_dir):
        self.id = bucket_id
        self.test_list = test_list
        self.sim_path = sim_path
        self.exe_path = exe_path
        self.exe_options = exe_options
        self.stats_dir = stats_dir

# Main loop

def main():
    argc = len(sys.argv)
    exe_path = ""
    exe_options = []
    if argc >= 3:
        exe_path = sys.argv[2]
        exe_options = sys.argv[3:]
    elif argc != 2:
        print("Usage: python "+ sys.argv[0] + " test_name [<path_to_exe> [options] ]")
        exit(-1)
    else:
        exe_name = "pico_sim"
        if os.name == "nt":
            exe_name += ".exe"
        exe_path = os.path.join(".", exe_name)
    print("Executable: " + exe_path)
    test_name = sys.argv[1].strip()

    log_path = os.path.join(os.path.dirname(exe_path),"cclog")
    if not os.path.isdir(log_path):
        print("Not a directory: " + log_path)
        exit(-1)
    
    source_path = Path(__file__).resolve()
    source_dir = source_path.parent
    print("Source dir:", source_dir)
    solution_dir = source_dir.parent
    print("Solution dir:", solution_dir)
    tmp_dir = os.path.join(solution_dir,"tmp")
    print("tmp dir:", tmp_dir)
    stats_path = os.path.join(tmp_dir,"stats")
    print("Stats path: " + stats_path)

    if not os.path.isdir(stats_path):
        print("Not a directory: " + stats_path)
        exit(-1)

    script_path = os.path.abspath(os.path.dirname(sys.argv[0]))
    # print("Script_path: " + script_path)
    solution_path = os.path.dirname(script_path)
    # solution_path = os.path.join(script_path, "..")
    print("Solution path: " + str(solution_path))
    sim_path = os.path.join(solution_path, "sim_specs")
    print("Sim path: ", sim_path)
    # get the list of test files
    test_list = [ ]
    if test_name.endswith("*"):
        test_prefix = os.path.basename(test_name)
        test_prefix = test_prefix[:-1]
        for t_name in os.listdir(sim_path):
            if len(test_prefix) == 0 or t_name.startswith(test_prefix):
                test_list.append(t_name)
    else:
        test_list.append(test_name)
    print("Found " + str(len(test_list)) + " tests.")

    # now that we have a list, consider breaking it in multiple "buckets"
    # that can be run in parallel. Each subprocess will execute a fraction
    # of the list, writing the results in a separate file. When all is
    # done, list the results of the tests
    nb_process = os.cpu_count()
    print("Aiming for " + str(nb_process) + " processes")
    process_left = nb_process

    bucket_list = []
    first_test = 0
    next_test = 1
    bucket_id = 0
    while first_test < len(test_list):
        if process_left == 1:
            next_test = len(test_list)
        else:
            step = int((len(test_list) - first_test + process_left - 1) / process_left);
            next_test = first_test + step;
            if next_test > len(test_list):
                next_test = len(test_list)
        tb = test_bucket(bucket_id, test_list[first_test:next_test], sim_path, exe_path, exe_options, stats_path)
        bucket_list.append(tb)
        first_test = next_test
        bucket_id += 1
        process_left -= 1
    print("Prepared " + str(len(bucket_list)) + " buckets.")
    start_time = time.time()
    if len(bucket_list) > 1:
        ret = 0
        with concurrent.futures.ProcessPoolExecutor(max_workers = nb_process) as executor:
            future_to_bucket = {executor.submit(run_one_list, bucket):bucket for bucket in bucket_list }
            for future in concurrent.futures.as_completed(future_to_bucket):
                bucket = future_to_bucket[future]
                try:
                    bucket_ret = future.result()
                    if bucket_ret != 0:
                        ret = -1
                except Exception as exc:
                    print('Bucket ' + str(bucket.id) + ' generated an exception: %s' % (exc))
    else:
        ret = run_one_list(bucket_list[0])
        print("Loaded a single bucket")

    bucket_time = time.time()
    print("Complete in " + str(bucket_time - start_time))

    exit(ret)

if __name__ == '__main__':
    main()