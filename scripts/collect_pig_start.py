import os
import sys

#
file_res = sys.argv[1]
log_dir = sys.argv[2]
pig_start = [
    "ccc0c4cb", "cc19c4cb", "cc1ac4cb", "badfc4cb" ]

chaos_start = [
    "badf", "ed1ac4fb", "fadac4", "ccc0c41f"
]

with open(file_res, "wt") as F:
    # current_time, c4_state->nb_eras_delay_based_decrease, c4_state->nominal_cwin, c4_state->max_cwin, 
    # c4_state->rtt_min, c4_state->rtt_filter.sample_min,
    # c4_state->rtt_filter.sample_max, path_x->rtt_variant, path_x->smoothed_rtt,
    # c4_state->chaotic_jitter
    F.write( "file, c_time, nb_dec, cwin, rate, max_cwin, max_rate, rtt_min, s_min, s_max, variant, smoothed, chaotic, pig_war, pw_entry, is_pig, is_chaos\n")
    for log_file in os.listdir(log_dir):
        log_path = os.path.join(log_dir, log_file)
        for line in open(log_path, "r"):
            is_pig = "0"
            is_chaos = "0"
            for s in pig_start:
                if log_file.startswith(s):
                    is_pig = "1"
                    break
            for s in chaos_start:
                if log_file.startswith(s):
                    is_chaos = "1"
                    break

            F.write( log_file + "," + line.strip() + "," + is_pig + "," + is_chaos + "\n")
