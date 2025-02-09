#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# @Author: LinLanfeng —— NUDT

import os, threading, csv

RUN_HISTORY = "run_history.csv"


# delete run_history.csv
def del_csv():
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

def create_new_data(data_dict, path):
    with open(path, 'w', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=list(data_dict.keys()))
        writer.writeheader()
        writer.writerows([data_dict])
    return


# store the running time and result to run_history.csv
def store_data(time, result, changes):
    new_data= {
        "id": str(threading.get_ident()),
        "time": time,
        "result": result,
        "changes": changes
    }

    # if no run_history.csv, then create one
    existing_data = read_data()
    if existing_data == []:
        create_new_data(new_data, RUN_HISTORY)
        return

    # append new
    updated_data = [new_data]
    for row in existing_data:
        if row["id"] != new_data["id"]:
            updated_data.append(row)

    with open(RUN_HISTORY, 'a', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=list(updated_data[0].keys()))
        writer.writerows(updated_data)

# get the current time from run_history.csv
def get_current_time():
    existing_data = read_data()
    if existing_data == []:
        return -1
    last_row = existing_data[-1]
    return float(last_row["time"])

# get the current result from run_history.csv
def get_current_result():
    existing_data = read_data()
    if existing_data == []:
        return -1
    last_row = existing_data[-1]
    return float(last_row["result"])

# check the current result is nearly equal to old result
def is_nearly_equal(old_result, thres):
    return is_nearly_equal_to(get_current_result(), old_result, thres)

# check the new result is nearly equal to old result
# use relative error to compare
def is_nearly_equal_to(new_result, old_result, thres):
    threshold = float(thres)
    # if old_result is 0, then use absolute error to compare
    if old_result == 0:
        return abs(new_result) <= threshold
    # if old_result is not 0, then use relative error to compare
    relative_error = abs((new_result - old_result) / old_result)
    print("new result rel_error", relative_error, "run_time", get_current_time())
    return relative_error <= threshold