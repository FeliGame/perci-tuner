#!/usr/bin/env python3

# @Author: LinLanfeng —— NUDT

import os, time
import data_utils
import subprocess
import numpy as np
import config_utils
import re

PER_TUNE_PATH = os.path.join(os.getenv("PERCI_TUNER"), "build/PerciTuner.so")
PASSES = "search_functions,config,change_precision,ir_timing"
TEMP_BC_PATH = "temp_output.bc"
TEMP_CONFIG_FILE = "temp_config.json"
TIME_FILE_PATH = "./ir_timing.txt"
RUNNING_TIMES = 1

global run_config_result

# read the main function running time from the file 
# printed by ir_timing pass
def read_main_running_time():
    with open(TIME_FILE_PATH, 'r') as file:
        for line in file:
            if line.startswith('main:'):
                return int(line.split(':')[1])
    return None

# read runned program's output
def read_result(text):
    pattern = r"(-?[\d.]+)"
    match = re.search(pattern, text)
    if match:
        float_number = match.group(1)
        return float_number
    else:
        return None


# run the current variable config and get the result
def transform(bitcode_file, var_config, diff):
    global run_config_result
    # write the config to the temp file 
    config_utils.print_config(var_config, TEMP_CONFIG_FILE)

    # run LLVM passes
    #opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='search_functions,config,change_precision' $FLAGS output.bc -o temp_output.bc
    command = ['opt','-load-pass-plugin', PER_TUNE_PATH, "-S", "-passes=" + PASSES, "--target-config=" + TEMP_CONFIG_FILE , bitcode_file, "-o", TEMP_BC_PATH]
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=None)
    # return -1 if running LLVM passes fails
    if retval != 0:
        return -1

    time = 0

    # run modified bitcode
    for i in range(RUNNING_TIMES):
        command = ['lli', TEMP_BC_PATH]
        try:
            output = subprocess.check_output(command, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            # return -2 if crashed when run modified bitcode
            return -2
        current_time = read_main_running_time()
        # return -3 if the time is not found
        if current_time == None:
            return -3
        time += read_main_running_time()
        if i == RUNNING_TIMES - 1:
            output_text = output.decode()
            print(output_text)
            run_config_result = read_result(output_text)
            if run_config_result is None:
                # return -4 if the result is not found
                return -4

    
    # store the running time and result
    data_utils.store_data(time/RUNNING_TIMES, run_config_result, diff)

    return 1
