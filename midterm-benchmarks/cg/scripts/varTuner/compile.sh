#!/bin/bash

##Usage: Script {test}.bc

my_dir="$(dirname "$0")"
source $my_dir/env.sh

# echo $my_dir

test=`echo $1 | sed 's/\(.*\).bc/\1/'`

#clang -c -emit-llvm $test.c -o temp_$test.bc
clang -g -O0 -emit-llvm -c ../common/wtime.c -o ../common/wtime.bc
clang -g -O0 -emit-llvm -c ../common/randdp.c -o ../common/randdp.bc
clang -g -O0 -emit-llvm -c ../common/print_results.c -o ../common/print_results.bc
clang -g -O0 -emit-llvm -c ../common/c_timers.c -o ../common/c_timers.bc

llvm-link -o json_$test.bc $test.bc ../common/wtime.bc ../common/randdp.bc ../common/print_results.bc ../common/c_timers.bc $auto_tuning/utils/doProfiling.bc $auto_tuning/utils/jsoncpp.bc
# llvm-link $test.bc $auto_tuning/utils/doProfiling.bc $auto_tuning/utils/jsoncpp.bc -o json_$test.bc 