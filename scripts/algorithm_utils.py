#!/usr/bin/env python3
# 算法共用的辅助函数
# @Author: LinLanfeng, Guocheng —— NUDT

import sys, json
import data_utils, transform


global run_times
run_times = 0
global min_time
min_time = float('inf')
global best_config_index
best_config_index = 0
global algorithm
algorithm = 'dd'
global is_mxp
is_mxp = False
global original_conf
runned_change_set = set()
global bitcode_name
global skip
skip = False


# update best config
def update_opt(updated_min_time):
    global best_config_index, min_time
    best_config_index = run_times
    min_time = updated_min_time


def compare_json(original, current, diff, prefix=""):
    """
    Recursively compare two JSON objects and store differences in diff.
    :param original: The original JSON object.
    :param current: The current JSON object.
    :param diff: The dictionary to store differences.
    :param prefix: The prefix for nested keys.
    """
    if isinstance(original, dict) and isinstance(current, dict):
        for key in original:
            if key not in current:
                diff[prefix + key] = f"{original[key]}->None"
            else:
                compare_json(original[key], current[key], diff, prefix + key + ".")
        for key in current:
            if key not in original:
                diff[prefix + key] = f"None->{current[key]}"
    elif isinstance(original, list) and isinstance(current, list):
        for i in range(max(len(original), len(current))):
            if i >= len(original):
                diff[prefix + str(i)] = f"None->{current[i]}"
            elif i >= len(current):
                diff[prefix + str(i)] = f"{original[i]}->None"
            else:
                compare_json(original[i], current[i], diff, prefix + str(i) + ".")
    else:
        if original != current:
            diff[prefix[:-1]] = f"{original}->{current}"


# run the current config and get the result if satisfy rel_err threshold
def run_config(bitcode, current_config, change_set, skip_runned=True):
    global run_times, original_conf, runned_change_set, is_mxp, skip
    
    # check if the config has been runned
    key = json.dumps(current_config, sort_keys=False)
    if skip_runned and key in runned_change_set: 
        print('>>> Perci-Tuner: The config has been runned, skipped...')
        skip = True
        return
    skip = False
    runned_change_set.add(key)

    # Initialize diff as an empty dictionary
    diff = {}

    # Compare current_config with original_conf and store differences in diff
    compare_json(original_conf, current_config, diff)
    run_times += 1
    result = transform.transform(run_times, bitcode, current_config, diff, is_mxp)
    satisfy_thres = True
    if result == 0:  # result not satisfy threshold
        satisfy_thres = False
    if result == -1:
        print(">>> Perci-Tuner Error: running LLVM passes failed!")
        sys.exit(1)
    elif result == -2:
        print(">>> Perci-Tuner Warning: running IR failed, ignored...")
        # sys.exit(1)
        return False
    elif result == -3:
        print(">>> Perci-Tuner Error: the running time is not found!")
        sys.exit(1)
    elif result == -4:
        print(">>> Perci-Tuner Error: the running result is not found!")
        sys.exit(1)
    elif result == -5:
        print(f">>> Perci-Tuner(Py) PUDD skip upcast-only config: INVALID_VAR_CNT:{result}")
        return False

    # save all configs into json
    if data_utils.ORIGINAL_RESULT == -1: return True
    if satisfy_thres:
        print(f">>> Perci-Tuner: VALID config{run_times}")
    else:
        print(f">>> Perci-Tuner: INVALID config{run_times}")

    return satisfy_thres
