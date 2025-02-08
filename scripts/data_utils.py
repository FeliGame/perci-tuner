#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# @Author: LinLanfeng —— NUDT

import os, threading, csv

TEMP_DATA_FILE = "temp_data.csv"


# delete temp_data.csv
def del_csv():
    if os.path.exists(TEMP_DATA_FILE):
        os.remove(TEMP_DATA_FILE)

# read datas from temp_data.csv
def read_data():
    if not os.path.exists(TEMP_DATA_FILE):
        return []
    try:
        with open(TEMP_DATA_FILE, 'r', newline='') as csvfile:
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


# store the running time and result to temp_data.csv
def store_data(time, result, changes):
    new_data= {
        "id": str(threading.get_ident()),
        "time": time,
        "result": result,
        "changes": changes
    }

    # if no temp_data.csv, then create one
    existing_data = read_data()
    if existing_data == []:
        create_new_data(new_data, TEMP_DATA_FILE)
        return

    # append new
    updated_data = [new_data]
    for row in existing_data:
        if row["id"] != new_data["id"]:
            updated_data.append(row)

    with open(TEMP_DATA_FILE, 'a', newline='') as csvfile:
        writer = csv.DictWriter(csvfile, fieldnames=list(updated_data[0].keys()))
        writer.writerows(updated_data)

# get the current time from temp_data.csv
def get_current_time():
    existing_data = read_data()
    if existing_data == []:
        return -1
    last_row = existing_data[-1]
    return float(last_row["time"])

# get the current result from temp_data.csv
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