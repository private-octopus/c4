# Media tests

## Basic test

The basic test is defined as a simulcast of audio as datagrams, and two video streams,
one low def and one mid def. The video streams are send in a manner similar to MoQ,
as series of "groups" conataining one I-Frame and the dependent P frames. The
configuratio is defined as:

* Data rate: 100 Mbps
* Latency (one way): 15 ms
* Audio stream: packets of 80 bytes, sent at 50 Hz
* Low def video: 30 fps, groups of 30 frames, I-Frame: 37500 bytes, P-Frame: 3750 bytes
* Mid def video: 30 fps, groups of 30 frames, I-Frame: 37500 bytes, P-Frame: 3750 bytes

We run this test for three different CC algorithms: C4, Cubic and BBR. We measure the queuing
delay observed on the audio and low definition frames. The results are:

| Queuing (ms)  |av/max| C4 | Cubic | BBR |
|------|----|-------|-------|-------|
| Audio | av | 1 | 1 | 2 |
| Audio | max | 14| 14 | 82 |
| Video Low | av | 1 | 1 | 1 |
| Video Low | max | 4 | 4 | 6 |
| Video High | av | 1 | 1 | 2 |
| Video High | max | 4 | 4 | 16 |


C4 and Cubic perform identically, and do not add much extra delay, while BBR does.
The worst delay comes from audio frames sent just after the beginning of group. At
that point, the code will queue two I-Frames (low def and then high def), and
about 10 ms later an audio frame (since the 30Hz and 60Hz frequencies are not in sync).
The audio frame will be queued in the network behind these two video frames.
C4 and Cubic send the data at close to line rate, but BBR applies some restrictive
pacing which results in peak delays.

## Low bandwidth test

This test configuration is identical to the basic test, except that the
line rate is lower and the latency a bit higher:

* Data rate: 10 Mbps
* Latency (one way): 20 ms
* Audio stream: packets of 80 bytes, sent at 50 Hz
* Low def video: 30 fps, groups of 30 frames, I-Frame: 37500 bytes, P-Frame: 3750 bytes
* Mid def video: 30 fps, groups of 30 frames, I-Frame: 37500 bytes, P-Frame: 3750 bytes

| Queuing (ms)  |av/max| C4 | Cubic | BBR |
|------|----|-------|-------|-------|
| Audio | av | 6 | 6 | 2 |
| Audio | max | 64 | 82 | 82 |
| Video Low | av | 7 | 7 | 5 |
| Video Low | max | 53 | 53 | 32 |
| Video High | av | 13 | 13 | 17 |
| Video High | max | 82 | 86 | 117 |

In this test, we see C4 performing better than Cubic for audio and for mid def 
video, on par with Cubic and slightly worse than BBR for low def video. Need
analysis of the difference with BBR!

## WiFi fading test

The WiFi test simulates a Wi-Fi link, with the device moving temporarily to
an area with low Wi-Fi coverage. We expect that the high definition video
will be dropped.

| Queuing (ms)  |av/max| C4 | Cubic | BBR |
|------|----|-------|-------|-------|
| Audio | av | 43 | 44 | 22 |
| Audio | max | 322 | 300 | 225 |
| Video Low | av | 93 | 115 | 102 |
| Video Low | max | 659 | 839 | 922 |
| Video High | av | 45 | 40 | 46 |
| Video High | max | 100 | 943 | 909 |

C4 performs worse than BBR but on par with Cubic for audio, and better than either BBR or
Cubic for low definition video. The performance of high speed video shows a much lower
max for C4 than for BBR or Cubic, probably because high speed video was cut off after
congestion was noticed.


## WiFi suspension test

The test simulates WiFi suspension.

| Queuing (ms)  |av/max| C4 | Cubic | BBR |
|------|----|-------|-------|-------|
| Audio | av | 10 | 10 | 10 |
| Audio | max | 189 | 188 | 186 |
| Video Low | av | 13 | 13 | 13 |
| Video Low | max | 191 | 188| 186 |
| Video High | av | 23 | 23 | 16 |
| Video High | max | 251 | 222 | 198 |

C4 performs better than Cubic on average and almost as well as BBR for this scenario, which is
encouraging because we did lot of work to improve BBR for this scenario. The difference
with BBR needs to be analyzed, and could probably be fixed. 