#!/bin/sh
#build last picoquic master (for Travis)

cd ..
git clone https://github.com/private-octopus/picoquic
cd picoquic
git checkout
git submodule init
git submodule update
cmake $CMAKE_OPTS .
make -j$(nproc) all
cd ..
