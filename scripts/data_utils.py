#!/usr/bin/env python3
# Handle run_history.csv

# @Author: LinLanfeng, Guocheng —— NUDT

import os, csv

RUN_HISTORY = "run_history.csv"


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
def store_data(time, result, changes):
    new_line= {
        "cycles": f"{time:.2e}",
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
    return is_nearly_equal_to(float(new_result), float(old_result), thres)


# check the new result is nearly equal to old result
# use relative error to compare
def is_nearly_equal_to(new_result, old_result, thres):
    threshold = float(thres)
    # if old_result is 0, then use absolute error to compare
    if old_result == 0:
        return abs(new_result) <= threshold
    # if old_result is not 0, then use relative error to compare
    relative_error = abs((new_result - old_result) / old_result)
    print (f"relative error: {relative_error}")
    return relative_error <= threshold