# Extract data from a set of qlog files.
#
# The tests of C4 generate a large number of qlog files, one for each connection.
# TODO:
# - parse the simulation spec.
#   - extract the qlog prefix, and the nominal bandwidth of the connection.
#   - check the type of simulation: media, bulk, compete.
# - using the qlog prefix, select the relevant qlog files in the qlog folder.
# - compute statistics for each qlog file.
#
# Solution: get qlogs from the qlog folder, write statictic in csv file, one line per exp:
#  test_case, rep, avg_rtt, max_rtt, duration, send_rate

import sys
import os
import json
import qlogparse
import time
import concurrent.futures
import traceback

def get_line_text(line):
    line = line.strip()
    parts = line.split(":")

    if len(line) == 0 or line[0] == '#':
        return None
    return line

class spec_data:
    def __init__(self, spec_file):
        self.spec_name = os.path.basename(spec_file)
        if self.spec_name.endswith(".txt"):
            self.spec_name = self.spec_name[:-4]
        self.spec_file = spec_file
        self.nb_connections = 0
        self.icid = ""
        self.data_rate_in_gbps = 0.0
        self.has_media = False

    def load_spec(self):
        for line in open(self.spec_file, "r"):
            line = line.strip()
            if len(line) == 0 or line[0] == '#':
                continue
            parts = line.split(":")
            if len(parts) < 2:
                continue
            head = parts[0].strip()
            text = parts[1].strip()

            if head == "nb_connections":
                self.nb_connections = int(text)
            elif head == "data_rate_in_gbps":
                self.data_rate_in_gbps = float(text)
            elif head == "icid":
                self.icid = text
            elif head == "media_stats_start" or \
                head == "media_latency_average" or \
                head == "media_latency_max" or \
                head == "media_excluded":
                self.has_media = True


def sim_spec_list(filter):
    # get the source directory
    current = os.path.dirname(os.path.abspath(__file__))
    print("current: " + current)
    # one level up to get the solution
    c4_dir = os.path.dirname(current)
    print("c4_dir: " + c4_dir)
    # look for "sim_spec"
    sim_spec_dir = os.path.join(c4_dir, "sim_specs")
    if not os.path.isdir(sim_spec_dir):
        print("sim_spec folder not found at: " + sim_spec_dir)
        return []
    # list the files in sim spec
    specs = dict()
    for f in os.listdir(sim_spec_dir):
        if not f.endswith(".txt"):
            continue
        if len(filter) > 0:
            match = False
            for prefix in filter:
                if f.startswith(prefix):
                    match = True
                    break
            if not match:
                continue
        path = os.path.join(sim_spec_dir, f)
        spec = spec_data(path)
        spec.load_spec()
        if spec.icid in specs:
            print("Duplicate spec for icid: " + spec.icid + ", " + spec.spec_name + ", " + specs[spec.icid].spec_name)
        else:
            specs[spec.icid] = spec
    return specs

class qlog_trace_data:
    def __init__(self, spec_name, data_rate):
        self.spec_name = spec_name
        self.data_rate = data_rate
        self.qlogs = dict()
        self.bg_qlogs = dict()

    def add_qlog(self, qlog_file, rep):
        if rep in self.qlogs:
            print("Duplicate rep: " + str(rep) + " for spec: " + self.spec_name)
        else:
            self.qlogs[rep] = qlog_file

    def add_bg_qlog(self, qlog_file, rep):
        if rep in self.bg_qlogs:
            print("Duplicate background rep: " + str(rep) + " for spec: " + self.spec_name)
        else:
            self.bg_qlogs[rep] = qlog_file

class qlog_data():
    def __init__(self, qlog_dir, data_dir):
        self.specs = sim_spec_list(["c4_", "cubic_", "bbr_"])
        self.qlog_dir = qlog_dir
        self.data_dir = data_dir
        self.data_dict = dict()

    def add_qlogs(self):
        for f in os.listdir(self.qlog_dir):
            if len(f) != 16 + len(".server.qlog"):
                continue
            if f.endswith("00.server.qlog"):
                is_background = False
            elif f.endswith("01.server.qlog"):
                is_background = True
            else:
                continue
            rep = int(f[12:14],16)
            cid_prefix = f[:8]
            if cid_prefix in self.specs:
                spec = self.specs[cid_prefix]
                if not spec.spec_name in self.data_dict:
                    self.data_dict[spec.spec_name] = \
                        qlog_trace_data(spec.spec_name, (int)(spec.data_rate_in_gbps * 1000000000))
                if is_background:
                    self.data_dict[spec.spec_name].add_bg_qlog(os.path.join(self.qlog_dir, f), rep)
                else:
                    self.data_dict[spec.spec_name].add_qlog(os.path.join(self.qlog_dir, f), rep)

class qdb_bucket:
    def __init__(self, qld, bucket_id, spec_name, time_start):
        self.qld = qld
        self.bucket_id = bucket_id
        self.spec_name = spec_name
        self.time_start = time_start

    def load(self):
        qtd = self.qld.data_dict[self.spec_name]
        output_file = os.path.join(self.qld.data_dir,"q_" + self.spec_name + ".csv")
        with open(output_file, "wt") as F:
            F.write("rep,time,ave_rtt,std_rtt,load,test\n")
            cst = qlogparse.connection_stats()
            bg_cst = qlogparse.connection_stats()
            for rep in qtd.qlogs:
                cst.load_qlog(qtd.qlogs[rep])
                send_rate = cst.send_rate
                if rep in qtd.bg_qlogs:
                    # A background connection competed for bandwidth during part
                    # of this run. Restrict the send rate used for "load" to the
                    # window where both connections were active, since the
                    # solo portions (before/after the background connection)
                    # aren't representative of competition. RTT stats are left
                    # over the full connection duration.
                    bg_cst.load_qlog(qtd.bg_qlogs[rep])
                    overlap_rate = cst.overlap_send_rate(bg_cst.start, bg_cst.end)
                    if overlap_rate is not None:
                        send_rate = overlap_rate
                load = send_rate / qtd.data_rate
                F.write(str(rep) + "," +  str(cst.duration) +
                        "," + str(cst.avg_rtt) +
                        "," + str(cst.std_rtt) +
                        "," + str(load) + "," + self.spec_name + "\n")


def load_qdb_bucket(bucket):
    bucket.load()
    return True



# Main program
if __name__ == "__main__":
    qld = qlog_data(sys.argv[1], sys.argv[2])
    qld.add_qlogs()
    buckets = []
    bucket_id = 0
    time_start = time.time()
    for spec_name in qld.data_dict:
        bucket = qdb_bucket(qld, bucket_id, spec_name, time_start)
        buckets.append(bucket)
        bucket_id += 1
    nb_buckets = bucket_id
    with concurrent.futures.ProcessPoolExecutor(max_workers = nb_buckets) as executor:
        future_to_bucket = {executor.submit(load_qdb_bucket, bucket):bucket for bucket in buckets }
        for future in concurrent.futures.as_completed(future_to_bucket):
            bucket = future_to_bucket[future]
            try:
                data = future.result()
                print('Bucket %d complete for scenario %s' % (bucket.bucket_id, bucket.spec_name))
            except Exception as exc:
                traceback.print_exc()
                print('Bucket %d (%s) generated an exception: %s' % (bucket.bucket_id, bucket.spec_name, exc))
