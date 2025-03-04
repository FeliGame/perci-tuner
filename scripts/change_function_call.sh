#!/bin/bash

# @Author: Guocheng —— NUDT

################################################################################
# Merge multi version of IR                                                    #
# sh run_multi_version.sh /path/to/other_version.bc func_name                  #
#                                                                              #
################################################################################

if ! opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='change_function_call' $1 -o $2; then
    echo "opt command failed. Exiting script."
    exit 1
fi

if ! lli changed.bc; then
    echo "lli command failed. Exiting script."
    exit 1
fi
