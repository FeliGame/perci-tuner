#!/usr/bin/env python3
# Execute commands

# @Author: LinLanfeng, Guocheng —— NUDT

import sys
import os
import time
import data_utils
import subprocess
import config_utils
import re
import platform
import math


COMPILER = 'clang'
ARCH = platform.machine()
PERCI_TUNER_PATH = os.path.join(os.getenv("PERCI_TUNER"), "build/PerciTuner.so")
PASSES = "change_math_call,change_variable_decl"
FP128_TO_DD = False
MIX_BINOP = True
RUNNING_TIMES = 1
RUNNING_FAIL_TIMES = 0
TIMEOUT_THRESHOLD = 60.0  # 超时阈值（秒）
TIME_MODE = "program"
LINK_LIBS = []
LINK_DIRS = []
RUNTIME_LIB_DIRS = []
CFLAGS = []
RUN_ARGS = []

# time & result in current run
current_run_time = float("inf")
current_run_wall_time = float("inf")
current_run_result = ""
show_history = False

KERNEL_TIME_PATTERN = re.compile(
    r"^\s*core_time_sec\s*=\s*"
    r"([+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"
    r"\s*$",
    re.IGNORECASE | re.MULTILINE,
)


# read runned program's output
def read_result(text):
    # 匹配字符串末尾的浮点数（十进制/十六进制）、科学计数法，去掉空白字符
    # 十六进制格式支持：±0x[0-9a-fA-F]+.[0-9a-fA-F]+p±\d+（如0x1.23p+4、-0x1.a5p-3）
    # 十进制格式支持：±\d+.?\d+(e±\d+)?（如123.45、-6.7e+8）
    pattern = r"([-+]?0x[0-9a-fA-F]+\.?[0-9a-fA-F]*(?:[pP][-+]?\d+)?|[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*$"
    match = re.search(pattern, text.strip(), re.IGNORECASE)  # 忽略大小写（处理0X、P等）
    if match:
        return match.group(1)  # 返回原始字符串（供后续判断十六进制格式）
    # 处理输出末尾的nan/inf（忽略大小写），保留为可读的非有限数值标记。
    special_match = re.search(r"([-+]?(?:nan|inf(?:inity)?))\s*$", text.strip(), re.IGNORECASE)
    if special_match:
        special_value = special_match.group(1).lower()
        if "nan" in special_value:
            return "nan"
        if special_value.startswith("-"):
            return "-inf"
        return "inf"
    return None


def read_kernel_time(text):
    matches = KERNEL_TIME_PATTERN.findall(text)
    if len(matches) != 1:
        return None

    kernel_time = float(matches[0])
    if not math.isfinite(kernel_time) or kernel_time < 0:
        return None
    return kernel_time


# run the current variable config and get the result
def transform(run_id, bitcode_file, var_config, diff, is_mxp):
    global current_run_time, current_run_wall_time, current_run_result, RUNNING_FAIL_TIMES
    # 确保目标目录存在
    target_dirs = {
        "config": "config",
        "IR": "IR",
        "exec": "exec"
    }
    for dir_name in target_dirs.values():
        os.makedirs(dir_name, exist_ok=True)
    
    # write the config to the temp file 
    CURRENT_CONFIG_NAME = os.path.join("config", str(run_id) + ".json")
    CURRENT_BC_NAME = os.path.join("IR", str(run_id) + ".bc")
    CURRENT_EXEC_NAME = os.path.join("exec", str(run_id))

    config_utils.print_config(var_config, CURRENT_CONFIG_NAME)
    # run LLVM passes
    command = ['opt', 
               '-load-pass-plugin', PERCI_TUNER_PATH, 
               "-S", 
               "-passes=" + PASSES + 
               (",record_result" if is_mxp else "") + ",remove_pragma" + 
               (",fp128_to_dd" if ((run_id > 1 or ARCH == "x86_64") and FP128_TO_DD) else ""),
               "--enable-mix-binop=" + str(MIX_BINOP),
            #    "--print-dd=" + str(FP128_TO_DD),
               "--variable-config=" + CURRENT_CONFIG_NAME, 
               "--function-config=" + CURRENT_CONFIG_NAME, 
               bitcode_file, "-o", CURRENT_BC_NAME] 
               
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=subprocess.DEVNULL)  # Don't output LLVM.err()
    # return -1 if running LLVM passes fails
    if retval != 0:
        return -1
    
    # generate executable
    command = [COMPILER, '-o', CURRENT_EXEC_NAME, CURRENT_BC_NAME] + CFLAGS + LINK_LIBS + LINK_DIRS + RUNTIME_LIB_DIRS
    if (run_id > 1 or ARCH == "x86_64") and FP128_TO_DD:
        command += [os.path.join(os.getenv("PERCI_TUNER"), "lib/dd_real/libautop-x86.a" if ARCH == "x86_64" else "lib/dd_real/libautop.a"), '-lm']
    retval = subprocess.call(command, stdin=None, stdout=None, stderr=subprocess.DEVNULL)  # Don't output LLVM.err()
    if retval != 0:
        return -2

    os.chmod(CURRENT_EXEC_NAME, 0o755)  # 添加读、写、执行权限

    time_list = []  # 改为存储所有运行时间的列表
    wall_time_list = []
    collect_wall_time = run_id == 1 and TIME_MODE == "kernel"
    logged = False  # 是否登记过运行结果/误差
    satisfy_thres = True  # 是否满足误差阈值

    # run modified bitcode
    for i in range(RUNNING_TIMES):
        command = ['./' + CURRENT_EXEC_NAME] + RUN_ARGS
        try:
            # 使用perf_counter统计执行时间
            start_time = time.perf_counter()
            completed = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=TIMEOUT_THRESHOLD if run_id > 1 else None,
                check=True,
            )
            end_time = time.perf_counter()
            output = completed.stdout
            stderr_output = completed.stderr
            
            if not output.strip():
                print(">>> Perci-Tuner(Py) Error: Program has no output, search failed!")
                sys.exit(1)
            output_text = output.decode()
            stderr_text = stderr_output.decode()
            # if stderr_text:
            #     print(stderr_text, end="", file=sys.stderr, flush=True)
            if TIME_MODE == "kernel":
                execution_time = read_kernel_time(stderr_text)
                if execution_time is None:
                    return -3
            else:
                execution_time = end_time - start_time
            if not logged:  # 同一程序只需登记一次运行结果/误差
                logged = True
                # output = output.splitlines()[-1]  # 取最后一行
                output_val = read_result(output_text)
                if output_val is None:
                    return -4
                if is_mxp:  # 如果是mxp模式，则output接收到的最后一行是误差值，不是结果值
                    numeric_output_val = float(output_val)
                    if not math.isfinite(numeric_output_val) or numeric_output_val > config_utils.thres:
                        satisfy_thres = False
                        current_run_result = output_val
                        current_run_time = float("nan")
                        break
                elif data_utils.ORIGINAL_TIME != float('inf') and not data_utils.is_nearly_equal(output_val, data_utils.ORIGINAL_RESULT, config_utils.thres):
                    satisfy_thres = False
                    current_run_result = output_val
                    current_run_time = float("nan")
                    break
        except subprocess.TimeoutExpired:
            print(f">>> Perci-Tuner(Py) Error: Timeout after {TIMEOUT_THRESHOLD} seconds.")
            RUNNING_FAIL_TIMES += 1
            current_run_result = None
            if show_history:
                data_utils.store_data(run_id, float("inf"), "TIMEOUT", diff)
            return -2
        except subprocess.CalledProcessError as e:
            if e.stdout:
                print(e.stdout.decode(), end="")
            if e.stderr:
                print(e.stderr.decode(), end="", file=sys.stderr)
            print(f">>> Perci-Tuner(Py) Error: Command failed with code {e.returncode}: {' '.join(e.cmd)}")
            RUNNING_FAIL_TIMES += 1
            current_run_result = None
            if show_history:
                data_utils.store_data(run_id, float("inf"), "RUNTIME-ERROR", diff)
            return -2
        
        time_list.append(execution_time)
        if collect_wall_time:
            wall_time_list.append(end_time - start_time)
        
        if i == RUNNING_TIMES - 1:
            print('current result:', output_text)
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
    if collect_wall_time:
        wall_time_list.sort()
        if RUNNING_TIMES % 2 == 1:
            current_run_wall_time = wall_time_list[RUNNING_TIMES//2]
        else:
            current_run_wall_time = (wall_time_list[RUNNING_TIMES//2-1] + wall_time_list[RUNNING_TIMES//2]) / 2

    if show_history:
        data_utils.store_data(run_id, current_run_time, current_run_result, diff, is_mxp)
    return 1
