#!/bin/bash

# @Author: Guocheng —— NUDT

####################################
# Build PerciTuner automatically   #
####################################

cd $PERCI_TUNER
rm -rf build
mkdir build
cd build
cmake ..
make