#!/usr/bin/env python3
# Handle run_history.csv

# @Author: LinLanfeng, Guocheng —— NUDT

import os, csv

RUN_HISTORY = "run_history.csv"
ORIGINAL_TIME = float('inf')  # running time of original program
ORIGINAL_RESULT = -1.0        # running result of original program

# delete run_history.csv
def delete_data():
    if os.path.exists(RUN_HISTORY):
        os.remove(RUN_HISTORY)


# read datas from run_history.csv
def read_data():
    if not os.path.exists(RUN_HISTORY):
        return []
    try:
        with open(RUN_HISTORY, 'r', newline='') as csvfile:
            reader = csv.DictReader(csvfile)
            return list(reader)
    except csv.Error:
        return []


def create_new_data(new_line, path):
    with open(path, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=list(new_line.keys()))
        writer.writeheader()
        writer.writerow(new_line)
    return


# store the running time and result to run_history.csv
def store_data(run_id, time, result, changes):
    new_line= {
        "run_id": run_id,
        "acc_rate": f"{((ORIGINAL_TIME - time) / time):.6%}",
        "cycles": f"{time:.2e}",
        "rel_err": f"{get_rel_err(result, ORIGINAL_RESULT):e}",
        "result": result,
        "changes": changes
    }

    # if no run_history.csv, then create one
    existing_data = read_data()
    if existing_data == []:
        create_new_data(new_line, RUN_HISTORY)
        return

    with open(RUN_HISTORY, 'a', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=list(new_line.keys()))
        writer.writerow(new_line)


# check the current result is nearly equal to old result
def is_nearly_equal(new_result, old_result, thres):
    # 屏蔽错误字段（RUNTIME_ERROR或TIMEOUT等）
    try:
        float(new_result)
        float(old_result)
    except (ValueError, TypeError):
        return False
    return get_rel_err(new_result, old_result) <= float(thres)


# use relative error to compare
def get_rel_err(new_result, old_result):    
    n = float(new_result)
    o = float(old_result)
    # if old_result is 0, then use absolute error to compare
    if o == 0:
        return abs(n)
    # if old_result is not 0, then use relative error to compare
    relative_error = abs((n - o) / o)
    return relative_error