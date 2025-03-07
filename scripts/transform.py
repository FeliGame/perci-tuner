#!/usr/bin/env python3
# Execute commands

# @Author: LinLanfeng, Guocheng —— NUDT

import sys
import os
import data_utils
import subprocess
import config_utils
import re


PERCI_TUNER_PATH = os.path.join(os.getenv("PERCI_TUNER"), "build/PerciTuner.so")
PASSES = "change_precision,change_function_call,ir_timing"
TEMP_BC_PATH = "temp_output.bc"
TEMP_CONFIG_FILE = "temp_config.json"
TIME_FILE_PATH = "ir_timing.txt"
RUNNING_TIMES = 1

# time & result in current run
current_run_time = float("inf")
current_run_result = float("inf")
show_history = False


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
    elif "nan" or "inf" in text:
        return sys.maxsize
    else:
        return None


# run the current variable config and get the result
def transform(bitcode_file, var_config, diff):
    global current_run_time, current_run_result
    # write the config to the temp file 
    config_utils.print_config(var_config, TEMP_CONFIG_FILE)

    # run LLVM passes
    command = ['opt', '-load-pass-plugin', PERCI_TUNER_PATH, "-S", "-passes=" + PASSES, "--target-config=" + TEMP_CONFIG_FILE , bitcode_file, "-o", TEMP_BC_PATH]
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=subprocess.DEVNULL)  # Don't output LLVM.err()
    # return -1 if running LLVM passes fails
    if retval != 0:
        return -1

    time = 0

    # run modified bitcode
    for i in range(RUNNING_TIMES):
        command = ['lli', '-load', '/usr/local/lib/libgsl.so', TEMP_BC_PATH]
        try:
            output = subprocess.check_output(command, stderr=None)
        except subprocess.CalledProcessError as e:
            # return -2 if crashed when run modified bitcode
            return -2
        current_time = read_main_running_time()
        # return -3 if the time is not found
        if current_time == None:
            return -3
        time += current_time
        if i == RUNNING_TIMES - 1:
            output_text = output.decode()
            print(output_text)
            current_run_result = read_result(output_text)
            if current_run_result is None:
                # return -4 if the result is not found
                return -4

    current_run_time = time/RUNNING_TIMES
    if show_history:
        data_utils.store_data(current_run_time, current_run_result, diff)

    return 1
