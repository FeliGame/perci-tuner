#!/usr/bin/env python3
# Execute commands

# @Author: LinLanfeng, Guocheng —— NUDT

import sys
import os
import data_utils
import subprocess
import config_utils
import re
import json

PERCI_TUNER_PATH = os.path.join(os.getenv("PERCI_TUNER"), "build/PerciTuner.so")
PASSES = "change_variable_decl,change_function_call"
TEMP_BC_PATH = "temp_output.bc"
TEMP_CONFIG_FILE = "temp_config.json"
TIME_FILE_PATH = "ir_timing.txt"
RUNNING_TIMES = 1
RUNNING_FAIL_TIMES = 0
TIMEOUT_THRESHOLD = 5.0  # 超时阈值（秒）
LINK_LIBS = []
LINK_DIRS = []
OPTIMIZE_LEVEL = "-O2"

# time & result in current run
current_run_time = 0.0
current_run_result = ''
show_history = False


# read the main function running time from the file 
# printed by ir_timing pass
def read_main_running_time():
    with open(TIME_FILE_PATH, 'r') as file:
        for line in file:
            if line.startswith('main:'):
                return int(line.split(':')[1])
    return None


# 分析程序直接输出的数值结果（Perci-Tuner原始版本）
def read_result(text):
    # 只匹配字符串末尾的浮点数或科学计数法，并去掉空白字符
    pattern = r"([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*$"
    match = re.search(pattern, text.strip())
    if match:
        float_number = match.group(1)
        return float_number
    elif "nan" or "inf" in text:
        return sys.maxsize
    else:
        return None


# 分析HPL-AI程序输出结果，摘自比赛平台case_judge.py
def analyze_hpl_output(output_text):
    global current_run_result, current_run_time
    lines = output_text.splitlines()
    pass_rate = 0
    flops = []
    rate_exist = False
    
    # 比赛平台预设的最大、最小和平均 T0 值
    max_T0 = 9.6609
    min_T0 = 4.5471
    mean_T0 = 7.5516

    for line in lines:
        if "Not supported in DTRSM." in line:
            rate_exist = False
            break
        if "Rate" in line:
            pass_rate = float(line.strip().split(":")[-1].strip("%")) / 100
            rate_exist = True
        if "Smallest" in line:
            temps = line.split("=")[-1].split(",")

    if not rate_exist:
        pass_rate = 0
        final_marks = 0
    else:
        max_flops = float(temps[2].strip().split()[0])
        min_flops = float(temps[0].strip().split()[0])
        mean_flops = float(temps[1].strip().split()[0])

        flops_marks = 100 * (0.6 * max_flops / max_T0 + 0.3 * mean_flops / mean_T0 + 0.1 * min_flops / min_T0) / 2
        final_marks = pass_rate * 100 * 0.4 + flops_marks * 0.6
        final_marks = round(final_marks, 2)

    result = {
        "verdict": "AC" if rate_exist else "WA",
        "rank": final_marks,
    }
    current_run_result = rate_exist
    current_run_time = final_marks
    return result


# run the current variable config and get the result
def transform(run_id, bitcode_file, var_config, diff, original_time):
    global current_run_time, current_run_result, RUNNING_FAIL_TIMES
    # write the config to the temp file 
    config_utils.print_config(var_config, TEMP_CONFIG_FILE)

    # run LLVM passes
    command = ['opt', '-load-pass-plugin', PERCI_TUNER_PATH, "-S", "-passes=" + PASSES, "--variable-config=" + TEMP_CONFIG_FILE , "--function-config=" + TEMP_CONFIG_FILE, bitcode_file, "-o", TEMP_BC_PATH]
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=subprocess.DEVNULL)  # Don't output LLVM.err()
    # return -1 if running LLVM passes fails
    if retval != 0:
        return -1
    
    # generate executable
    command = ['clang', OPTIMIZE_LEVEL, '-o', 'temp_output', 'temp_output.bc'] + LINK_LIBS + LINK_DIRS
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=subprocess.DEVNULL)  # Don't output LLVM.err()
    if retval != 0:
        return -1

    time_list = []  # 改为存储所有运行时间的列表

    # run modified bitcode
    for i in range(RUNNING_TIMES):
        command = ['./temp_output', '10', '128', '1280']
        try:
            output = subprocess.check_output(command, stderr=None, timeout=TIMEOUT_THRESHOLD)
            if not output.strip():
                print(">>> Perci-Tuner(Py) Error: Program has no output, search failed!")
                sys.exit(1)
        except subprocess.TimeoutExpired:
            print(f">>> Perci-Tuner(Py) Error: Timeout after {TIMEOUT_THRESHOLD} seconds.")
            RUNNING_FAIL_TIMES += 1
            if show_history:
                data_utils.store_data(run_id, float("inf"), "TIMEOUT", diff)
        except subprocess.CalledProcessError as e:
            print(e.output.decode())
            print(f">>> Perci-Tuner(Py) Error: Command failed with code {e.returncode}: {' '.join(e.cmd)}")
            RUNNING_FAIL_TIMES += 1
            if show_history:
                data_utils.store_data(run_id, float("inf"), "RUNTIME-ERROR", diff)
            return -2
        
        current_time = read_main_running_time()
        if current_time == None:
            return -3
        
        time_list.append(current_time)
        
        if i == RUNNING_TIMES - 1:
            output_text = output.decode()
            print(output_text)
            # current_run_result = read_result(output_text)
            # if current_run_result is None:
            #     return -4

            # 分析程序输出结果
            analysis_result = analyze_hpl_output(output_text)
            print(json.dumps(analysis_result, ensure_ascii=False))

    # 计算中位数（HPL-AI的rank一次性计算）
    # time_list.sort()
    # if RUNNING_TIMES % 2 == 1:
    #     current_run_time = time_list[RUNNING_TIMES//2]
    # else:
    #     current_run_time = (time_list[RUNNING_TIMES//2-1] + time_list[RUNNING_TIMES//2]) / 2

    if show_history:
        data_utils.store_data(run_id, current_run_time, current_run_result, diff)

    return 1
