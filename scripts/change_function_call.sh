#!/bin/bash

# @Author: Guocheng —— NUDT

################################################################################
# Change IR's function call according to config.json                           #
# sh change_function_call.sh /path/to/before_change.bc /path/to/after_change.bc#
#                                                                              #
################################################################################

if ! opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='change_function_call' $1 -o $2; then
    echo "opt command failed. Exiting script."
    exit 1
fi

if ! lli $2; then
    echo "lli command failed. Exiting script."
    exit 1
fi
