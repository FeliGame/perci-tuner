#!/usr/bin/env python3
# Handle run_history.csv

# @Author: LinLanfeng, Guocheng —— NUDT

import os, csv, decimal

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
def store_data(run_id, time, result, changes, is_mxp=False):
    new_line= {
        "run_id": run_id,
        "acc_rate": f"{((ORIGINAL_TIME - time) / time):.6%}",
        "cycles": f"{time:.2e}",
        "rel_err": result if is_mxp else f"{get_rel_err(result, ORIGINAL_RESULT):e}",
        "result": None if is_mxp else result,
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
        # 使用decimal替换float来检查输入值，避免精度损失
        decimal.getcontext().traps[decimal.Inexact] = False  # 允许不精确转换
        decimal.getcontext().traps[decimal.InvalidOperation] = True  # 捕获无效操作
        
        # 将输入值转换为字符串再转为Decimal，确保精确表示
        decimal.Decimal(str(new_result))
        decimal.Decimal(str(old_result))
        # 也转换阈值为Decimal类型
        decimal_thres = decimal.Decimal(str(thres))
    except (decimal.InvalidOperation, ValueError, TypeError):
        return False
    
    # 调用修改后的get_rel_err函数计算相对误差
    rel_err = get_rel_err(new_result, old_result)
    
    # 如果相对误差是inf，直接返回False
    if rel_err == float('inf'):
        return False
    
    # 使用Decimal进行比较，确保精度
    try:
        return decimal.Decimal(str(rel_err)) <= decimal_thres
    except (decimal.InvalidOperation, ValueError):
        # 如果转换失败，退回到浮点数比较
        return rel_err <= float(thres)


# use relative error to compare
def get_rel_err(new_result, old_result):    
    if new_result == "RUNTIME-ERROR" or old_result == "RUNTIME-ERROR":
        return float('inf')
    try:
        # 将输入转换为字符串，以便分析其精度需求
        new_str = str(new_result)
        old_str = str(old_result)
        
        # 计算需要的精度基于输入值的最大位数
        precision = 10  # 默认精度
        
        if '.' in new_str:
            # 小数点后位数 + 整数部分位数
            new_precision = len(new_str.split('.')[0]) + len(new_str.split('.')[1])
            precision = max(precision, new_precision)
        else:
            # 整数的位数
            precision = max(precision, len(new_str))
        
        if '.' in old_str:
            old_precision = len(old_str.split('.')[0]) + len(old_str.split('.')[1])
            precision = max(precision, old_precision)
        else:
            precision = max(precision, len(old_str))
        
        # 设置decimal上下文的精度
        decimal.getcontext().prec = precision
        
        # 转换为Decimal类型
        n = decimal.Decimal(new_str)
        o = decimal.Decimal(old_str)
        
        # 计算相对误差
        if o == 0:
            return float(abs(n))
        
        relative_error = abs((n - o) / o)
        return float(relative_error)
    except (decimal.InvalidOperation, ValueError, TypeError):
        # 处理无法转换为decimal的情况
        return float('inf')