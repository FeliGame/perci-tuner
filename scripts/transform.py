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
CHECK_RESULT_PATH = os.path.join(os.getenv("PERCI_TUNER"), "lib/check_result")
PASSES = "change_variable_decl,change_function_call,ir_timing"
TEMP_BC_PATH = "temp_output.bc"
TEMP_CONFIG_FILE = "temp_config.json"
TIME_FILE_PATH = "ir_timing.txt"
RUNNING_TIMES = 1
RUNNING_FAIL_TIMES = 0
TIMEOUT_THRESHOLD = 5.0  # 超时阈值（秒）

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


# run the current variable config and get the result
def transform(run_id, bitcode_file, var_config, diff, is_mxp, check_error):
    global current_run_time, current_run_result, RUNNING_FAIL_TIMES
    # write the config to the temp file 
    config_utils.print_config(var_config, TEMP_CONFIG_FILE)

    # run LLVM passes
    command = ['opt', 
               '-load-pass-plugin', PERCI_TUNER_PATH, 
               "-S", 
               # record_result加在ir_timing后面防止被记入用时, remove_pragma必须在record_result之后
               "-passes=" + PASSES + ",record_result,remove_pragma",
               "--variable-config=" + TEMP_CONFIG_FILE , 
               "--function-config=" + TEMP_CONFIG_FILE, 
               "--is-original=false", 
               bitcode_file, "-o", TEMP_BC_PATH] if is_mxp else \
            ['opt', 
               '-load-pass-plugin', PERCI_TUNER_PATH, 
               "-S", 
               "-passes=" + PASSES + ",remove_pragma", 
               "--variable-config=" + TEMP_CONFIG_FILE , 
               "--function-config=" + TEMP_CONFIG_FILE, 
               bitcode_file, "-o", TEMP_BC_PATH] 
               
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=subprocess.DEVNULL)  # Don't output LLVM.err()
    # return -1 if running LLVM passes fails
    if retval != 0:
        return -1
    
    # generate executable
    command = ['clang', '-O2', '-o', 'temp_output', 'temp_output.bc', 
            '-lgsl', '-lblas', '-lm'] + \
            ([f'-L{CHECK_RESULT_PATH}', f'-lcheck_result', f'-Wl,-rpath={CHECK_RESULT_PATH}'] if is_mxp else [])
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=subprocess.DEVNULL)  # Don't output LLVM.err()
    print('generate executable', retval)
    if retval != 0:
        return -1

    time_list = []  # 改为存储所有运行时间的列表
    test_result_once = not check_error  # 是否检查一次符合误差阈值（结果固定没必要重复运行，只需测试一次）
    satisfy_thres = True  # 是否满足误差阈值

    # run modified bitcode
    for i in range(RUNNING_TIMES):
        command = ['./temp_output']
        try:
            output = subprocess.check_output(command, stderr=None, timeout=TIMEOUT_THRESHOLD)
            if not output.strip():
                print(">>> Perci-Tuner(Py) Error: Program has no output, search failed!")
                sys.exit(1)
            if not test_result_once:
                test_result_once = True
                output = output.splitlines()[-1]  # 取最后一行
                output_text = output.decode()
                output_val = read_result(output_text)
                if is_mxp:  # 如果是mxp模式，则output接收到的最后一行是误差值，不是结果值
                    print('current_error', output_val)
                    if float(output_val) > config_utils.thres:
                        satisfy_thres = False
                        break
                elif not data_utils.is_nearly_equal(output_val, data_utils.ORIGINAL_RESULT, config_utils.thres):
                    satisfy_thres = False
                    break
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
            current_run_result = read_result(output_text)
            if current_run_result is None:
                return -4

    if not satisfy_thres:
        if show_history:
            data_utils.store_data(run_id, float('inf'), current_run_result, diff, is_mxp)
        return 0
    
    # 计算中位数
    time_list.sort()
    if RUNNING_TIMES % 2 == 1:
        current_run_time = time_list[RUNNING_TIMES//2]
    else:
        current_run_time = (time_list[RUNNING_TIMES//2-1] + time_list[RUNNING_TIMES//2]) / 2

    if show_history:
        data_utils.store_data(run_id, current_run_time, current_run_result, diff, is_mxp)
    return 1
