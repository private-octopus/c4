# Script for testing the C4/C4R code.
#
# For each test case starting with C4 (or C4R) in the sim_case folder, 
# run the test up to 100 times, or until it fails. For each trial,
# get a log in the cclog repository.
#
# arguments: test_loop.pt [path_of_the_exe]


import os
import sys


# print the current os

argc = len(sys.argv)
exe_path = ""
if argc == 3:
    exe_path = sys.argv[2]
elif argc != 2:
    print("Usage: python "+ sys.argv[0] + " test_name [<path_to_exe>]")
else:
    test_name = sys.argv[1].strip()
    exe_name = "pico_sim"
    if os.name == "nt":
        exe_name += ".exe"
    exe_path = os.path.join(".", exe_name)
print("Executable: " + exe_path)

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

for t_name in test_list:
    test_path = os.path.join(sim_path, t_name)
    if not os.path.isfile(test_path):
        print (test_path + " is not a file!")
        exit -1;
    ret = 0
    cmd = exe_path + " " + test_path
    for x in range(0, 100):
        ret = os.system(cmd)
        if ret != 0:
            print(t_name + " returns " + str(ret) + " after " + str(x) + " trials.")
            break
    if ret == 0:
        print("All 100 trials of " + t_name + " pass.")
