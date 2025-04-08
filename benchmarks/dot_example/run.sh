#!/bin/bash

if [ $# -ne 1 ]
then
	echo "$0 n"
	exit
fi

n=$1
make clean &> /dev/null
echo "n = $n"

echo "------------------ FP128 test ------------------"
make FP128=1 &> /dev/null
./fp128_test $n
echo

echo "------------------ FP64  test ------------------"
make D=1 &> /dev/null
./fp64_test $n
echo
