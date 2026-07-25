#!/usr/bin/env python3

# @Author: LinLanfeng, Guocheng —— NUDT

import math
import config_utils, transform, algorithm_utils

# the initial divide number for delta debugging change set
INITIAL_DIV_NUM = 2

# is every search types empty
def is_type_empty(change_set):
    for change in change_set:
        if len(change["search_types"]) > 0:
            return False
    return True


# change each element's type to the highest precision in change set
def change_to_highest_precision(change_set):
    for c in change_set:
        t = c["search_types"]
        if len(t) > 0:
            c["type"] = t[-1]


# change each element's type to the 2nd highest precision in change set
def change_to_2nd_highest_precision(change_set):
    for c in change_set:
        t = c["search_types"]
        if len(t) > 1:
            c["type"] = t[-2]


# remove the highest precision from the search types
def remove_highest_precision_from_selections(change_set):
    for c in change_set:
        if len(c["search_types"]) > 0:
            c["search_types"].pop()  # remove current highest precision


# can only be used in searching period
def remove_unchangable(change_set):
    new_change_set = []
    for c in change_set:
        t = c["search_types"]
        if len(t) > 2 and c["type"] != t[-1]:  # 一次搜索迭代至少要有两层精度
            new_change_set.append(c)
    return new_change_set

# 辅助函数：测试变化集并更新最佳配置
def test_and_update_change_set(bitcode, change_set, test_set, new_change_set):
    change_to_2nd_highest_precision(change_set)
    change_to_highest_precision(test_set)
    new_config = config_utils.rebuild(change_set)
    have_better_set = False

    # 执行测试配置
    run_passed = algorithm_utils.run_config(bitcode, new_config, change_set)
    current_result = transform.current_run_result
    if current_result is None:
        return new_change_set, False

    # 只有符合误差阈值时才更新最佳配置
    if run_passed and transform.current_run_time < algorithm_utils.min_time:
        have_better_set = True
        new_change_set = test_set.copy()
        algorithm_utils.update_opt(transform.current_run_time)
    
    return new_change_set, have_better_set


# using delta debugging algorithm to find the minimal change set
def search_minimal_change_set(bitcode, change_set, div_num):
    delta_change_set = []
    complement_delta_change_set = []
    # calculate the size of each part
    div_size = int(math.ceil(float(len(change_set))/float(div_num)))
    print(f'\nchange set size: {len(change_set)}, div num: {div_num}, div size: {div_size}')
    # divide change set and type set into div_num parts
    # and calculate each part's complement part

    for i in range(0, len(change_set), div_size):
        start = i
        end = i + div_size
        delta_change_set.append(change_set[start:end])
        complement_delta_change_set.append(change_set[:start] + change_set[end:])

    # iterate each delta change and its complement delta change 
    # to find the current minimal change set
    new_change_set = []
    have_better_set = False
    print(f'delta size: {len(delta_change_set)}, complement delta size: {len(complement_delta_change_set)}')

    # 串行处理delta搜索
    for i in range(len(delta_change_set)):
        delta_change = delta_change_set[i]
        complement_delta_change = complement_delta_change_set[i]

        # 测试delta变化集
        new_change_set, is_better_set = test_and_update_change_set(bitcode, change_set, delta_change, new_change_set)
        have_better_set |= is_better_set

        # 测试补集变化集
        new_change_set, is_better_set = test_and_update_change_set(bitcode, change_set, complement_delta_change, new_change_set)
        have_better_set |= is_better_set

    if have_better_set:
        # 还原最佳配置
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(new_change_set)
        config_utils.CURRENT_CONFIG = config_utils.rebuild(change_set)
        change_set = new_change_set
        if len(change_set) <= 1: return
        search_minimal_change_set(bitcode, change_set, INITIAL_DIV_NUM)
    else:
        print("none better in current iteration")
        change_to_highest_precision(change_set)
        if div_num < len(change_set):  # 二分细粒度搜索，直到不能细分为止
            div_num *= 2
            search_minimal_change_set(bitcode, change_set, div_num)

def run(bitcode, change_set):
    # searching until every search types is empty
    while not is_type_empty(change_set):
        # search which need highest precision & try to lower others' precision
        search_minimal_change_set(bitcode, change_set, INITIAL_DIV_NUM)
        # remove highest precision
        change_set = remove_unchangable(change_set)
        # remove the highest precision from the search types of the rest
        remove_highest_precision_from_selections(change_set)
    return config_utils.CURRENT_CONFIG
