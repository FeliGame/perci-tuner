#!/bin/bash

# @Author: Guocheng —— NUDT

########################
# Minimize Perci-Tuner #
########################

cd $PERCI_TUNER
find . -type f -name "*.json" -exec rm {} \;
find . -type f -name "*.bc" -exec rm {} \;
find . -type f -name "*.csv" -exec rm {} \;
find . -type f -name "*.txt" -not -name "CMakeLists.txt" -exec rm {} \;
# 仅删除根目录下无小数点的文件
find . -maxdepth 1 -type f -regex './[^.]+$' -exec rm {} \;