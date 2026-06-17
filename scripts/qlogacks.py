# parse a qlog file
#
# we want to draw graphs combining several logs, to show the results
# of competition between various congestion control algorithms
#
# we would like the logs to show:
# - CWIN
# - Bytes in flight
# - latest RTT sample
# - cumulative data
#
# we are getting these data from parsing the qlog file.

import sys
import json
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

class ack_state:
    def __init__(self):
        self.nb_received = 0
        self.time_first = 0
        self.first_recv = 0
        self.packet_tolerance = 0
        self.max_ack_delay = 0
        self.reordering_threshold = 0

    def sent_ack(self, ack_time, highest):
        print ("ack, " + str(ack_time) + ", "  + str(highest) + ", " \
              + str(highest - self.first_recv + 1) + ", " + str(self.packet_tolerance) \
              + ", " + str(ack_time - self.time_first) + ", " + str(self.max_ack_delay))
        self.nb_received = 0
        self.time_first = ack_time
        self.first_recv = highest

    def recv_packet(self, recv_time, sequence_number):
        if self.nb_received == 0:
            self.time_first = recv_time
            self.first_recv = sequence_number
        self.nb_received += 1

    def recv_ack_frequency(self, recv_time, packet_tolerance, max_ack_delay, reordering_threshold):
        self.packet_tolerance = packet_tolerance
        self.max_ack_delay = max_ack_delay
        self.reordering_threshold = reordering_threshold



class qlog_event:
    def __init__(self):
        self.event_time = 0
        self.category=""
        self.event=""
        self.data=None

    def load_event(self, ev, ef, reference_time):
        is_good = True
        if len(ef) != len(ev):
            print("error. Only " + str(len(ev)) + " elements in event: " + str(ev))
            is_good = False
        else:
            for i in range(0, len(ef)):
                evt = ef[i]
                if evt == 'relative_time':
                    self.event_time = ev[i] + reference_time
                elif evt == 'category':
                    self.category = ev[i]
                elif evt == 'event':
                    self.event = ev[i]
                elif evt == 'data':
                    self.data = ev[i]
                else:
                    print("Unexpected event element: " + evt + ": " + str(ev[i]))
                    is_good = False
        return is_good

    def print_event(self):
        print("[ " + str(self.event_time) + ", " + self.category + ", " + self.event + ", data ]")

class qlog_ack_trace:
    def __init__(self):
        self.ef = []
        self.reference_time = 0
        self.ack_state = ack_state()

    def load_event_fields(self, ef):
        self.ef = ef
        print(str(ef))

    def load_common(self, cf):
        if "reference_time" in cf:
            self.reference_time = int(cf["reference_time"])
            print("reference_time:" + str(self.reference_time))
        else:
            print(str(cf))

    def load(self, trc):
        for x in trc:
            print(x)
            if x == "event_fields":
                self.load_event_fields(trc[x])
            elif x == "common_fields":
                self.load_common(trc[x])
            elif x == "events":
                print(str(len(trc[x])) + " events")
                evts = trc[x]
                for i in range(0, len(trc[x])):
                    evx = qlog_event()
                    if not evx.load_event(evts[i], self.ef, self.reference_time):
                        print("Error load event " + str(i))
                        break
                    else:
                        # [0, "transport", "packet_sent", { "packet_type": "initial", "header":
                        # { "packet_size": 162, "packet_number": 0, "payload_length": 132, "scid": "030405060708090a", "dcid": "0203040506070809" },
                        # "frames": [{ 
                        # "frame_type": "ack", "ack_delay": 0, "acked_ranges": [[0, 0]]}, { 
                        # "frame_type": "crypto", "offset": 0, "length": 123}]}],
                        #
                        # [26793, "transport", "packet_sent", { "packet_type": "1RTT", "header":
                        # { "packet_size": 1424, "packet_number": 8, "dcid": "0203040506070809" },
                        # "frames": [{ 
                        # "frame_type": "stream", "id": 8, "offset": 4283, "length": 1410, "fin": false , "has_length": false, "begins_with": "bbbcbdbebfc0c1c2"}]}],
                        #
                        

                        if evx.category == "transport":
                           if evx.event == "packet_sent":
                                # get the "frames" field in the packet header
                                # check whether the is an "ack" field
                                # self.ack_state.ack_update(evx.event_time, evx.data)
                                if 'frames' in evx.data:
                                    for frame in evx.data['frames']:
                                        if 'frame_type' in frame:
                                            ft = frame['frame_type']
                                            if ft == 'ack':
                                                highest = 0
                                                for tup in frame['acked_ranges']:
                                                    if len(tup) == 2 and tup[1] > highest:
                                                        highest = tup[1]
                                                self.ack_state.sent_ack(evx.event_time, highest)
                                                #print("Sent: " + str(ft) + ": " + str(high_num))
                           elif evx.event == "packet_received":
                                # get the packet number
                                # evaluate conditions like 
                                # self.ack_state.recv_update(evx.event_time, evx.data)
                                if 'header' in evx.data:
                                    if 'packet_number' in evx.data['header']:
                                        h = evx.data['header']
                                        sequence_number = h['packet_number']
                                        self.ack_state.recv_packet(evx.event_time, sequence_number)
                                        # print("Recv: " +  'packet_number' + ": " + str(pn))
                                if 'frames' in evx.data:
                                    for frame in evx.data['frames']:
                                        if 'frame_type' in frame:
                                            ft = frame['frame_type']
                                            if ft == 'ack_frequency':
                                                # {'frame_type': 'ack_frequency', 'sequence_number': 23, 'packet_tolerance': 55, 'max_ack_delay': 10000, 'reordering_threshold': 0}
                                                packet_tolerance = 0
                                                max_ack_delay = 0
                                                reordering_threshold = 0
                                                if 'packet_tolerance' in frame:
                                                    packet_tolerance = frame['packet_tolerance']
                                                if 'max_ack_delay' in frame:
                                                    max_ack_delay = frame['max_ack_delay']
                                                if 'reordering_threshold' in frame:
                                                    reordering_threshold = frame['reordering_threshold']
                                                self.ack_state.recv_ack_frequency(evx.event_time, packet_tolerance, max_ack_delay, reordering_threshold)
            else:
                print(str(trc[x]))


def qlog_acks_parse(file_name):
    traces = []
    with open(file_name,"r") as F:
        qlog_object = json.load(F)
        for x in qlog_object:
            print(x)
            if x == "qlog_version":
                print(str(qlog_object[x]))
            elif x == "title":
                print(str(qlog_object[x]))
            elif x == "traces":
                trcs = qlog_object[x]
                for i in range(0, len(trcs)):
                    print("Traces[" + str(i) + "]:")
                    trace = qlog_ack_trace()
                    trace.load(trcs[i])
                    traces.append(trace)
    print("Loaded " + str(len(traces)) + " ack traces")
    return traces


# test part of the program
# assume each argument is a qlog file

for i in range(1, len(sys.argv)):
    print(sys.argv[i])
    qlog_acks_parse(sys.argv[i])


