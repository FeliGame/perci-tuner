#!/bin/bash

# @Author: Guocheng —— NUDT

########################
# Minimize Perci-Tuner #
########################

cd $PERCI_TUNER
find . -type f -name "*.json" -exec rm {} \;
find . -type f -name "*.bc" -exec rm {} \;
find . -type f -name "*.txt" -not -name "CMakeLists.txt" -exec rm {} \;