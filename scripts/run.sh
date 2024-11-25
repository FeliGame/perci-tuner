#!/bin/bash

# @Author: Guocheng —— NUDT

#####################################################
# Run PerciTuner automatically                      #
# sh run.sh /path/to/src --pass_parameter(Optional) #
#                                                   #
#####################################################

# Only keep path: path/to/a.cpp -> path/to/
SRC_PATH=$(echo "$1" | sed 's|/[^/]*$||')"/"
# Trim prefix & suffix: path/to/a.cpp-> a
SRC_NAME=$(echo "$1" | sed 's|.*/||' | sed 's|\.[^.]*$||')
# Trim suffix: path/to/a.cpp-> path/to/a
SRC_NO_SUFFIX=$SRC_PATH$SRC_NAME

echo "SRC_PATH: $SRC_PATH"
echo "SRC_NAME: $SRC_NAME"
echo "SRC_NO_SUFFIX: $SRC_NO_SUFFIX"

clang -emit-llvm -c $1 -o $SRC_NO_SUFFIX.bc

opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -time-passes -passes=CreateCallDependency $2 $SRC_NO_SUFFIX.bc -o $SRC_NO_SUFFIX.bc
# Run IR
lli $SRC_NO_SUFFIX.bc