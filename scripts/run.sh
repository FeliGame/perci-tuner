#!/bin/bash

# @Author: Guocheng —— NUDT

################################################################################
# Run PerciTuner automatically                                                 #
# sh run.sh /path/to/src1 /path/to/src2 ... "--pass_parameter=value"(Optional) #
#                                                                              #
################################################################################


FLAGS=""
BCS=""
for arg in "$@"
do
    if echo "$arg" | grep -q "^--"; then
        FLAGS="$FLAGS $arg"  # append arg to FLAGS
    else
        # Only keep path: path/to/a.cpp -> path/to/
        SRC_PATH=$(echo "$arg" | sed 's|/[^/]*$||')"/"
        # Trim prefix & suffix: path/to/a.cpp-> a
        SRC_NAME=$(echo "$arg" | sed 's|.*/||' | sed 's|\.[^.]*$||')
        # Trim suffix: path/to/a.cpp-> path/to/a
        SRC_NO_SUFFIX=$SRC_PATH$SRC_NAME

        echo "SRC_PATH: $SRC_PATH"
        echo "SRC_NAME: $SRC_NAME"
        echo "SRC_NO_SUFFIX: $SRC_NO_SUFFIX"

        clang -g -emit-llvm -c $arg -o $SRC_NO_SUFFIX.bc
        BCS="$BCS $SRC_NO_SUFFIX.bc"  # append bc name to BCS

    fi
done

llvm-link $BCS -o output.bc

opt -time-passes -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='search_functions,config,change_precision,ir_timing' $FLAGS output.bc -o output.bc

# Run IR
lli output.bc
