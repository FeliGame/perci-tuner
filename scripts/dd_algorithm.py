#!/usr/bin/env python3

# @Author: LinLanfeng, Guocheng —— NUDT

import math
import config_utils, data_utils, transform, algorithm_utils
import concurrent.futures  # 新增：并行处理库

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


# using delta debugging algorithm to find the minimal change set
def search_minimal_change_set(bitcode, change_set, div_num, threads):
    delta_change_set = []
    complement_delta_change_set = []
    # calculate the size of each part
    div_size = int(math.ceil(float(len(change_set))/float(div_num)))
    print(f'\nchange set size: {len(change_set)}, div num: {div_num}, div size: {div_size}')
    # divide change set and type set into div_num parts
    # and calculate each part's complement part

    # Guo's version
    for i in range(0, len(change_set), div_size):
        start = i
        end = i + div_size
        delta_change_set.append(change_set[start:end])
        complement_delta_change_set.append(change_set[:start] + change_set[end:])

    # iterate each delta change and its complement delta change 
    # to find the current minimal change set
    new_change_set = []
    new_div_num = div_num
    have_better_set = False
    print(f'delta size: {len(delta_change_set)}, complement delta size: {len(complement_delta_change_set)}')

    # 并行处理delta搜索的辅助函数
    def process_delta(i):
        delta_change = delta_change_set[i]
        complement_delta_change = complement_delta_change_set[i]
        local_better = False
        local_new_change = []
        local_min_time = algorithm_utils.min_time

        # 测试delta变化集
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(delta_change)
        new_config = config_utils.rebuild(change_set)
        if algorithm_utils.run_config(bitcode, new_config, change_set) and transform.current_run_time < local_min_time:
            local_better = True
            local_new_change = delta_change.copy()
            local_min_time = transform.current_run_time

        # 测试补集变化集
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(complement_delta_change)
        new_config = config_utils.rebuild(change_set)
        if algorithm_utils.run_config(bitcode, new_config, change_set) and transform.current_run_time < local_min_time:
            local_better = True
            local_new_change = complement_delta_change.copy()
            local_min_time = transform.current_run_time
            
        return (local_better, local_new_change, local_min_time)

    # 并行执行
    with concurrent.futures.ThreadPoolExecutor(max_workers=threads) as executor:
        results = list(executor.map(process_delta, range(len(delta_change_set))))
    
    # 处理并行结果
    for better, new_change, min_time in results:
        if better:
            if not have_better_set or min_time < algorithm_utils.min_time:
                have_better_set = True
                new_change_set = new_change
                algorithm_utils.min_time = min_time
                algorithm_utils.update_opt(config_utils.rebuild(change_set))

    if have_better_set:
        # 还原最佳配置
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(new_change_set)
        config_utils.CURRENT_CONFIG = config_utils.rebuild(change_set)
        div_num = new_div_num
        change_set = new_change_set
        if len(change_set) < 1: return
        search_minimal_change_set(bitcode, change_set, INITIAL_DIV_NUM, threads)
    else:
        print("none better in current iteration")
        change_to_highest_precision(change_set)
        if div_num < len(change_set):  # 二分细粒度搜索，直到不能细分为止
            div_num *= 2
            search_minimal_change_set(bitcode, change_set, div_num, threads)

def run(bitcode, change_set, threads):
    # searching until every search types is empty
    while not is_type_empty(change_set):
        # search which need highest precision & try to lower others' precision
        search_minimal_change_set(bitcode, change_set, INITIAL_DIV_NUM, threads)
        # remove highest precision
        change_set = remove_unchangable(change_set)
        # remove the highest precision from the search types of the rest
        remove_highest_precision_from_selections(change_set)
    return config_utils.CURRENT_CONFIG
