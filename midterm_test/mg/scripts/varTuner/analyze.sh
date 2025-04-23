#!/bin/bash

##Usage: Script json_{test}.bc

my_dir="$(dirname "$0")"
source $my_dir/env.sh

test=`echo $1 | sed 's/\(.*\).bc/\1/'`

# which opt

#instrument the bitcode file
opt -load-pass-plugin $auto_tuning/src/varDeps_new/Passes_new.so --passes=edge-profiling $1 > i_$1
opt -load-pass-plugin $auto_tuning/src/varDeps_new/Passes_new.so --passes=print-varDeps $1 > /dev/null
# opt -load $auto_tuning/src/varDeps/Passes.so -edge-profiling $1 > i_$1
# opt -load $auto_tuning/src/varDeps/Passes.so -print-varDeps $1 > /dev/null

#compile and run the instrumented file
# llc -march=aarch64 -filetype=asm i_$test.bc -o i_$test.s
# g++ -march=armv8-a i_$test.s -O2 -lm -llog -L$CORVETTE_PATH/logging -o i_$test.out
# rm i_$test.s 

clang++ i_$test.bc -o i_$test.out -lm -llog -L/home/xuzicai/HiFPTuner-llvm17/precimonious/logging

echo "start to run the instrumented file"
start=`date +%s.%N`
time ./i_$test.out
end=`date +%s.%N`
runtime=$(echo "$end - $start" | bc)
echo "instrumented file finished its execution in $runtime seconds" > auto-tuning_analyze_time.txt

#analyze the weighted dependence graph
#start=`date +%s.%N`
#time python -O $auto_tuning/src/graphAnalysis/varDepGraph_pro.py
#end=`date +%s.%N`
#runtime=$(echo "$end - $start" | bc)
#echo "auto-tuning finishes in $runtime seconds" >> auto-tuning_analyze_time.txt
