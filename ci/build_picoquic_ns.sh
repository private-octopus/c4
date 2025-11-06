#!/bin/sh
#build last picoquic master (for Travis)

cd ..
git clone https://github.com/private-octopus/picoquic_ns
cd picoquic_ns
cmake $CMAKE_OPTS .
make -j$(nproc) all
pwd
ls
cd ..
