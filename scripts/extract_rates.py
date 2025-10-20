# Extracting the traces added as messages into the qlog.

import sys
import pandas as pd

# main

header_set = [ 'time', 'rate', 'n-rate', 'bytes', 'rtt', 'send-delay', 'nominal-rtt', 'state', 'path-bw', 'is_cc' ]
line_set = []

for line in open(sys.argv[1], "r"):
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
        line_set.append(r)
df = pd.DataFrame(line_set, columns=header_set)
if df.shape[0] > 1:
    df.to_csv(sys.argv[2])
    print("Saved " + str(df.shape[0]) + " lines.")