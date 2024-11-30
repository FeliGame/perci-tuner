#!/bin/bash

# @Author: Guocheng —— NUDT

####################################
# Build PerciTuner automatically   #
####################################

cd $PERCI_TUNER
# rm -rf build
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake ..
make