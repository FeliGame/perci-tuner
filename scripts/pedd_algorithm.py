#!/usr/bin/env python3

# @Author: Guocheng —— NUDT

import math
import config_utils, data_utils, transform, algorithm_utils

# the initial divide number for delta debugging change set
INITIAL_DIV_NUM = 2
invalid_result_map = dict() # 超误差阈值结果->降低精度变量集的映射

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

# 辅助函数：比较两个 change_set 元素是否相等
def change_element_equal(e1, e2):    
    # 检查是否为同一类型的元素
    if 'func' in e1 and 'func' in e2:  # 局部变量类型
        return e1['func'] == e2['func'] and e1['fvar_index'] == e2['fvar_index']
    elif 'caller' in e1 and 'caller' in e2:  # 函数调用类型
        return e1['caller'] == e2['caller'] and e1['fcall_index'] == e2['fcall_index']
    elif 'struct' in e1 and 'struct' in e2:  # 结构体成员类型
        return e1['struct'] == e2['struct'] and e1['member_index'] == e2['member_index']
    # 如果都不属于上述类型，尝试比较整个字典
    return e1 == e2

# 辅助函数：计算两个 change_set 的交集
def get_change_set_intersection(set1, set2):
    intersection = []
    for e1 in set1:
        for e2 in set2:
            if change_element_equal(e1, e2):
                intersection.append(e1)
    return intersection

# 辅助函数：从 change_set 中移除指定元素或元素列表
def remove_from_change_set(change_set, elements):
    # 如果elements不是列表，转换为列表
    if not isinstance(elements, list):
        elements = [elements]
    return [e for e in change_set if not any(change_element_equal(e, el) for el in elements)]

# 辅助函数：测试变化集并更新结果映射
def test_and_update_change_set(bitcode, change_set, test_set, new_change_set, have_better_set, div_num):
    change_to_2nd_highest_precision(change_set)
    change_to_highest_precision(test_set)
    new_config = config_utils.rebuild(change_set)
    
    # 执行测试配置，记录结果无论是否符合阈值
    run_passed = algorithm_utils.run_config(bitcode, new_config, change_set)
    current_result = transform.current_run_result
    
    # 计算降低后的变量集: change_set - test_set
    reduced_set = remove_from_change_set(change_set, test_set)  # 从change_set中移除test_set的元素
    
    pruned_change_set = None  # 用于标记是否需要更新change_set
    if not algorithm_utils.skip and not run_passed:
        if current_result in invalid_result_map:
            # 运行结果已出现过，计算交集
            existing_set = invalid_result_map[current_result]
            current_set = reduced_set
            intersection = get_change_set_intersection(existing_set, current_set)
            invalid_result_map[current_result] = intersection
            print(f'{current_result}\'s Intersection: {intersection}')
        else:
            print(f'New result encountered: {current_result}')
            # 运行结果第一次出现，记录降低后的变量集
            invalid_result_map[current_result] = reduced_set.copy()
            intersection = reduced_set
        if len(intersection) == 1:
            # 将该元素从当前change_set中清除
            element_to_remove = intersection[0]
            pruned_change_set = remove_from_change_set(change_set, element_to_remove)
            print(f'Remove config with {element_to_remove}')
    
    # 只有符合误差阈值时才更新最佳配置
    if run_passed and transform.current_run_time < algorithm_utils.min_time:
        have_better_set = True
        new_change_set = test_set.copy()
        algorithm_utils.min_time = transform.current_run_time
        algorithm_utils.update_opt(new_config)
    
    return new_change_set, have_better_set, pruned_change_set


# using delta debugging algorithm to find the minimal change set
def search_minimal_change_set(bitcode, change_set, div_num):
    delta_change_set = []
    complement_delta_change_set = []
    # calculate the size of each part
    div_size = int(math.ceil(float(len(change_set))/float(div_num)))
    print(f'\nchange set size: {len(change_set)}, div num: {div_num}, div size: {div_size}')

    for i in range(0, len(change_set), div_size):
        start = i
        end = i + div_size
        delta_change_set.append(change_set[start:end])
        complement_delta_change_set.append(change_set[:start] + change_set[end:])

    new_change_set = []
    have_better_set = False
    print(f'delta size: {len(delta_change_set)}, complement delta size: {len(complement_delta_change_set)}')

    # 串行处理delta搜索
    pruned_change_set = None
    for i in range(len(delta_change_set)):
        delta_change = delta_change_set[i]
        complement_delta_change = complement_delta_change_set[i]

        # 测试delta变化集
        new_change_set, is_better_set, temp_updated = test_and_update_change_set(bitcode, change_set, delta_change, new_change_set, have_better_set, div_num)
        have_better_set |= is_better_set
        if temp_updated:
            pruned_change_set = temp_updated
            break

        # 测试补集变化集
        new_change_set, is_better_set, temp_updated = test_and_update_change_set(bitcode, change_set, complement_delta_change, new_change_set, have_better_set, div_num)
        have_better_set |= is_better_set
        if temp_updated:
            pruned_change_set = temp_updated
            break

    # 处理找到的 better_set 和 pruned_change_set
    if have_better_set:
        # 还原最佳配置
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(new_change_set)
        config_utils.CURRENT_CONFIG = config_utils.rebuild(change_set)
        change_set = new_change_set
        # 如果同时存在 pruned_change_set，需要将其与 better_set 结合
        if pruned_change_set:
            # 从新的 change_set（即 better_set）中移除导致超阈值的元素
            change_set = [e for e in change_set if any(change_element_equal(e, pc_e) for pc_e in pruned_change_set)]
            print(f'Pruned better set: {change_set}')
        if len(change_set) <= 1: return
        search_minimal_change_set(bitcode, change_set, INITIAL_DIV_NUM)
    elif pruned_change_set:
        # 如果没有找到 better_set 但有需要更新的 change_set，使用更新后的集合并重新搜索
        search_minimal_change_set(bitcode, pruned_change_set, div_num)
    else:
        print("none better in current iteration")
        change_to_highest_precision(change_set)
        if div_num < len(change_set):  # 二分细粒度搜索，直到不能细分为止
            div_num *= 2
            search_minimal_change_set(bitcode, change_set, div_num)

def run(bitcode, change_set):
    global invalid_result_map
    # searching until every search types is empty
    while not is_type_empty(change_set):
        # search which need highest precision & try to lower others' precision
        search_minimal_change_set(bitcode, change_set, INITIAL_DIV_NUM)
        # remove highest precision
        change_set = remove_unchangable(change_set)
        # remove the highest precision from the search types of the rest
        remove_highest_precision_from_selections(change_set)
    return config_utils.CURRENT_CONFIG
