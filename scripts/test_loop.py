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


def run_one_test(t_name, sim_path, exe_path, bucket_id):
    ret = 0
    test_path = os.path.join(sim_path, t_name)
    if not os.path.isfile(test_path):
        print (str(bucket_id) + ": " + test_path + " is not a file!")
        ret = -1;
    else:
        cmd = exe_path + " " + test_path
        for x in range(0, 100):
            ret = os.system(cmd)
            if ret != 0:
                print(str(bucket_id) + ": " + t_name + " returns " + str(ret) + " after " + str(x) + " trials.")
                break
    if ret == 0:
        print(str(bucket_id) + ": " + "All 100 trials of " + t_name + " pass.")
    return ret

def run_one_list(tb):
    ret = 0
    for t_name in tb.test_list:
        if run_one_test(t_name, tb.sim_path, tb.exe_path, tb.id) != 0:
            ret = -1
    return ret

class test_bucket:
    def __init__(self, bucket_id, test_list, sim_path, exe_path):
        self.id = bucket_id
        self.test_list = test_list
        self.sim_path = sim_path
        self.exe_path = exe_path

# Main loop

def main():
    argc = len(sys.argv)
    exe_path = ""
    if argc == 3:
        exe_path = sys.argv[2]
    elif argc != 2:
        print("Usage: python "+ sys.argv[0] + " test_name [<path_to_exe>]")
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
        test_prefix = test_name[:-1]
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
        tb = test_bucket(bucket_id, test_list[first_test:next_test], sim_path, exe_path)
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
