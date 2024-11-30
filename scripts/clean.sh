#!/bin/bash

# @Author: Guocheng —— NUDT

########################
# Minimize Perci-Tuner #
########################

cd $PERCI_TUNER
find . -type f -name "searched_functions.txt" -exec rm {} \;
find . -type f -name "*.json" -exec rm {} \;
find . -type f -name "*.bc" -exec rm {} \;