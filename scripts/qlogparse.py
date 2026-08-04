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
#import pandas as pd
#import matplotlib.pyplot as plt
#import numpy as np

class cc_state:
    def __init__(self):
        self.event_time = 0
        self.cwnd = 0
        self.bytes_in_flight = 0
        self.pacing_rate = 0
        self.smoothed_rtt = 0
        self.min_rtt = 0
        self.latest_rtt = 0
        self.app_limited = 0

    def cc_update(self, event_time, cc_data):
        self.event_time = event_time
        for x in cc_data:
            if x == 'cwnd':
                self.cwnd = int(cc_data[x])
            elif x == 'bytes_in_flight':
                self.bytes_in_flight = int(cc_data[x])
            elif x == 'pacing_rate':
                self.pacing_rate = int(cc_data[x])
            elif x == 'smoothed_rtt':
                self.smoothed_rtt = int(cc_data[x])
            elif x == 'min_rtt':
                self.min_rtt = int(cc_data[x])
            elif x == 'latest_rtt':
                self.latest_rtt = int(cc_data[x])
            elif x == 'app_limited':
                self.app_limited = int(cc_data[x])
            else:
                print("Unexpected cc element: " + x)

    def cc_vector(self):
        v = [
            self.event_time,
            self.cwnd,
            self.bytes_in_flight,
            self.pacing_rate,
            self.smoothed_rtt,
            self.min_rtt,
            self.latest_rtt,
            self.app_limited ]
        return v

    def cc_headers():
        headers = [
            'event_time',
            'cwnd',
            'bytes_in_flight',
            'pacing_rate',
            'smoothed_rtt',
            'min_rtt',
            'latest_rtt',
            'app_limited'
        ]
        return headers

class data_sent:
    def __init__(self):
        self.event_time = 0
        self.bytes_sent = 0

    def data_update(self, event_time, ev_data):
        self.event_time = event_time
        self.bytes_sent = 0
        for x in ev_data:
            if x == 'byte_length':
                self.bytes_sent = int(ev_data[x])
            else:
                pass

    def data_vector(self):
        v = [self.event_time, self.bytes_sent]
        return v

    def data_headers():
        headers = [
            'event_time',
            'bytes_sent'
        ]
        return headers


class qlog_event:
    def __init__(self):
        self.event_time = 0
        self.category = ""
        self.event = ""
        self.data = ""

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

class qlog_trace:
    def __init__(self):
        self.ef = []
        self.reference_time = 0
        self.events = []
        self.cc_state = cc_state()
        self.cc_log = []
        self.data_sent = data_sent()
        self.sent_log = []

    def load_event_fields(self, ef):
        self.ef = ef
        #print(str(ef))

    def load_common(self, cf):
        if "reference_time" in cf:
            self.reference_time = int(cf["reference_time"])
            #print("reference_time:" + str(self.reference_time))
        else:
            #print(str(cf))
            pass

    def load(self, trc):
        for x in trc:
            #print(x)
            if x == "event_fields":
                self.load_event_fields(trc[x])
            elif x == "common_fields":
                self.load_common(trc[x])
            elif x == "events":
                #print(str(len(trc[x])) + " events")
                evts = trc[x]
                for i in range(0, len(trc[x])):
                    evx = qlog_event()
                    if not evx.load_event(evts[i], self.ef, self.reference_time):
                        print("Error load event " + str(i))
                        break
                    else:
                        self.events.append(evx)
                        if evx.category == "recovery" and evx.event == "metrics_updated":
                            self.cc_state.cc_update(evx.event_time, evx.data)
                            self.cc_log.append(self.cc_state.cc_vector())
                        elif evx.category == "transport" and evx.event == "datagram_sent":
                            self.data_sent.data_update(evx.event_time, evx.data)
                            self.sent_log.append(self.data_sent.data_vector())

                #print("Loaded " + str(len(self.events)) + " events.")
                #print("Loaded " + str(len(self.cc_log)) + " cc_logs.")
                #print("Loaded " + str(len(self.sent_log)) + " sent_logs.")
            else:
                #print("Other event:" + str(trc[x]))
                pass


def qlog_parse(file_name):
    traces = []
    with open(file_name,"r") as F:
        qlog_object = json.load(F)
        for x in qlog_object:
            #print(x)
            if x == "qlog_version":
                #print(str(qlog_object[x]))
                pass
            elif x == "title":
                #print(str(qlog_object[x]))
                pass
            elif x == "traces":
                trcs = qlog_object[x]
                for i in range(0, len(trcs)):
                    #print("Traces[" + str(i) + "]:")
                    trace = qlog_trace()
                    trace.load(trcs[i])
                    traces.append(trace)
    #print("Loaded " + str(len(traces)) + " traces")
    return traces


# Analyse the qlog to compute the average and max RTT,
# the duration of the connection and the average send rate in bits per second.

class connection_stats:
    def __init__(self):
        self.avg_rtt = 0
        self.max_rtt = 0
        self.std_rtt = 0
        self.duration = 0
        self.send_rate = 0

    def load_qlog(self, qlog_file):
        trc = qlog_parse(qlog_file)
        rtt_sum2 = 0
        rtt_sum = 0
        rtt_nb = 0
        start = 0
        end = 0
        data_sent = 0
        for tre in trc:
            for ev in tre.cc_log:
                rtt = ev[6]  # latest_rtt
                if rtt > 0:
                    rtt_nb += 1
                    rtt_sum += rtt
                    rtt_sum2 += rtt*rtt
                if rtt > self.max_rtt:
                    self.max_rtt = rtt
            if rtt_nb == 0:
                self.avg_rtt = 0
                self.std_rtt = 0
            else:
                self.avg_rtt = int(rtt_sum / rtt_nb)
                v = (rtt_sum2 / rtt_nb) - (self.avg_rtt * self.avg_rtt)
                if v > 0:
                    self.std_rtt = int(v ** 0.5)
                else:
                    self.std_rtt = 0

            for ev in tre.sent_log:
                if start == 0:
                    start = ev[0]
                end = ev[0]
                data_sent += ev[1]

            self.duration = end - start
            if self.duration > 0:
                self.send_rate = int(8000000*data_sent / self.duration)

            # we only process the first trace in a set
            break