# parse a media file and plot graphs
# The media file is a csv file
import sys
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

color_list = [  'blue', 'green', 'orange', 'red', 'cyan', 'olive',  'purple', \
             'brown', 'pink', 'gray', 'yellow', 'violet', 'magenta', 'lime', 'chartreuse', 'salmon' ]

def color_pick(rank, stream):
    stream_color = color_list[rank + 4]
    if stream == 'a1':
        stream_color = 'blue'
    elif stream == 'vlow':
        stream_color = 'green'
    elif stream == 'vmid':
        stream_color = 'orange'
    elif stream == 'vlow':
        stream_color = 'red'
    else:
        stream_color = color_list[(rank + 4)%color_list]
    return stream_color
   
def do_graph(media_df, image_file, title):
    is_first = True
    #list the number of media streams
    streams = media_df['stream'].unique()

    # extract one sub_df per media
    sub_df = []
    for stream in streams:
        sub_df.append(media_df[media_df['stream'] == stream])
    legend_list = []
    for i in range(0, len(streams)):
        stream = streams[i]
        stream_color = color_pick(i, stream)
        if len(sub_df[i]) > 0:
            if is_first:
                axa = sub_df[i].plot.scatter(x="time_start", y="delay", alpha=0.5, color=stream_color)
            else:
                sub_df[i].plot.scatter(ax=axa, x="time_start", y="delay", alpha=0.5, color=stream_color)
            is_first = False
            legend_list.append(stream)
    plt.title(title)
    plt.legend(legend_list, loc='upper right')
    if len(image_file) == 0:
        plt.show()
    else:
        plt.savefig(image_file)
    plt.close()

# main
if len(sys.argv) < 2 or len(sys.argv) > 4:
    print("Usage: python mediagraph.py <media report csv> [<graph file> [title]]")
    exit (1)
media_report_file = ""
image_file = ""
title = "Queuing delays"
if len(sys.argv) > 2:
    media_report_file = sys.argv[1]
    if len(sys.argv) > 3:
        image_file = sys.argv[2]
        if len(sys.argv) == 4:
            title = sys.argv[3]
   

#read csv file
media_df = pd.read_csv(media_report_file, skipinitialspace=True)
# create a time in second 
media_df = media_df.assign(time_start = lambda x: x['init_time']/1000000)
# add a delay column
media_df = media_df.assign(delay = lambda x: (x['recv_time'] -x['init_time']))
# compute the min and then latency
min_delay = int(media_df['delay'].min()/1000)*1000
print("Min delay: " + str(min_delay))

#add a queue column
media_df = media_df.assign(queue = lambda x: (x['delay'] - min_delay))
do_graph(media_df, image_file, title)
