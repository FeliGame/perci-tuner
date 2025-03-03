#!/bin/bash

# @Author: Guocheng —— NUDT

################################################################################
# Merge multi version of IR                                                    #
# sh run_multi_version.sh /path/to/other_version.bc func_name                  #
#                                                                              #
################################################################################

FLAGS="--ir-multi=$2 --function-multi=$3"

# generate mvselect.bc
if ! clang -S -emit-llvm -fno-discard-value-names -o mvselect.bc lib/mvselect.c; then
    echo "generate mvselect.bc failed. Exiting script."
    exit 1
fi

# link mvselect.bc with original IR
if ! llvm-link $1 mvselect.bc -o output+mvselect.bc; then
    echo "llvm-link command failed. Exiting script."
    exit 1
fi

llvm-dis output+mvselect.bc -o output+mvselect.bc

if ! opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='merge_multi' $FLAGS output+mvselect.bc -o merged.bc; then
    echo "opt command failed. Exiting script."
    exit 1
fi

if ! lli merged.bc; then
    echo "lli command failed. Exiting script."
    exit 1
fi