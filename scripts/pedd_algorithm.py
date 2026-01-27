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

# 辅助函数：生成元素的哈希键
def get_element_hash_key(element):
    # 根据元素类型生成唯一可哈希的键
    if 'global' in element:  # 全局变量类型
        return ('global', element['gvar_index'])
    elif 'func' in element:  # 局部变量类型
        return ('func', element['func'], element['fvar_index'])
    elif 'caller' in element:  # 函数调用类型
        return ('caller', element['caller'], element['fcall_index'])
    elif 'struct' in element:  # 结构体成员类型
        return ('struct', element['struct'], element['member_index'])
    # 如果都不属于上述类型，使用整个字典的冻结集作为键
    return tuple(sorted(element.items()))

# 辅助函数：比较两个 change_set 元素是否相等
def change_element_equal(e1, e2):    
    # 使用哈希键比较更高效
    return get_element_hash_key(e1) == get_element_hash_key(e2)

# 辅助函数：计算两个 change_set 的交集
def get_change_set_intersection(set1, set2):
    # 选择较小的集合作为基础，减少哈希表大小
    if len(set1) > len(set2):
        set1, set2 = set2, set1
    
    # 创建哈希键到元素的映射，O(n)时间
    set1_map = {get_element_hash_key(e): e for e in set1}
    intersection = []
    
    # 遍历第二个集合，检查是否在映射中，O(m)时间
    for e in set2:
        key = get_element_hash_key(e)
        if key in set1_map:
            intersection.append(set1_map[key])
    
    return intersection

# 辅助函数：从 change_set 中移除指定元素或元素列表
def remove_from_change_set(change_set, elements):
    # 如果elements不是列表，转换为列表
    if not isinstance(elements, list):
        elements = [elements]
    # 将elements转换为哈希键集合，O(m)时间
    elements_keys = {get_element_hash_key(element) for element in elements}
    # 过滤change_set中不在elements_keys中的元素，O(n)时间
    return [e for e in change_set if get_element_hash_key(e) not in elements_keys]

# 辅助函数：判断 set1 是否是 set2 的子集
def is_subset_of(set1, set2):
    if len(set1) > len(set2):
        return False
    # 将set2转换为哈希键集合，O(m)时间
    set2_keys = {get_element_hash_key(element) for element in set2}
    # 检查set1的每个元素是否都在set2中，O(n)时间
    for element in set1:
        if get_element_hash_key(element) not in set2_keys:
            return False
    return True

# 辅助函数：测试变化集并更新结果映射
def test_and_update_change_set(bitcode, change_set, test_set, new_change_set, div_num):
    change_to_2nd_highest_precision(change_set)
    change_to_highest_precision(test_set)
    new_config = config_utils.rebuild(change_set)
    have_better_set = False
    # 计算降低后的变量集: change_set - test_set
    reduced_set = remove_from_change_set(change_set, test_set)
    # print(f'reduced_set: {reduced_set}\n')
    # 策略二：如果reduced_set 是 invalid_result_map 中任意 value 的超集则跳过，因为结果必然是超误差阈值的
    for existing_reduced_set in invalid_result_map.values():
        # print(f'existing reduced_set:{existing_reduced_set}')
        if is_subset_of(existing_reduced_set, reduced_set):
            print(f'Skipping execution')
            return new_change_set, False, None
    
    # 执行测试配置，记录结果无论是否符合阈值
    run_passed = algorithm_utils.run_config(bitcode, new_config, change_set)
    current_result = transform.current_run_result  # 从change_set中移除test_set的元素
    if current_result is None:
        return new_change_set, False, None

    pruned_change_set = None  # 用于标记是否需要更新change_set
    intersection = []
    if not algorithm_utils.skip and not run_passed:
        if current_result in invalid_result_map:
            # 运行结果已出现过，计算交集
            existing_set = invalid_result_map[current_result]
            intersection = get_change_set_intersection(existing_set, reduced_set)
            invalid_result_map[current_result] = intersection
            print(f'{current_result}\'s Intersection: {intersection}')
        else:
            print(f'New result encountered: {current_result}')
            # 运行结果第一次出现，记录降低后的变量集
            invalid_result_map[current_result] = reduced_set.copy()
            intersection = reduced_set
        # 策略一：精度敏感变量剪枝
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
    bottleneck_found = False  # 是否找到瓶颈变量
    for i in range(len(delta_change_set)):
        delta_change = delta_change_set[i]
        complement_delta_change = complement_delta_change_set[i]

        # 测试delta变化集
        new_change_set, is_better_set, temp_updated = test_and_update_change_set(bitcode, change_set, delta_change, new_change_set, div_num)
        have_better_set |= is_better_set
        if temp_updated:
            bottleneck_found = True
            pruned_change_set = temp_updated
            # 先暂存当前 pruned_change_set

        # 测试补集变化集
        new_change_set, is_better_set, temp_updated = test_and_update_change_set(bitcode, change_set, complement_delta_change, new_change_set, div_num)
        have_better_set |= is_better_set
        if temp_updated:
            bottleneck_found = True
            if pruned_change_set is not None:
                # 与上次pruned_change_set取并集
                # 创建当前pruned_change_set的哈希键集合，O(n)时间
                existing_keys = {get_element_hash_key(e) for e in pruned_change_set}
                # 只添加新的元素，O(m)时间
                for element in temp_updated:
                    if get_element_hash_key(element) not in existing_keys:
                        pruned_change_set.append(element)
            else:
                pruned_change_set = temp_updated
        
        if bottleneck_found:
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
