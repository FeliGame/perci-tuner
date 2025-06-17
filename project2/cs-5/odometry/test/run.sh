#!/bin/bash
clang -o odometry odometry.c  -lm > /dev/null 2>&1
echo "源程序编译完成，开始运行"
./odometry
echo "混合精度程序成功运行"
echo
clang -o mix-precision_odometry mix-precision_odometry.c  -lm > /dev/null 2>&1
echo "混合精度程序编译完成，开始运行"
./mix-precision_odometry
echo "混合精度程序成功运行"