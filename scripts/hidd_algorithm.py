#!/usr/bin/env python3
# (HiFPTuner)分层delta-debugging算法实现
# @Author: LiLei —— NUDT

import math
import transform, config_utils, data_utils, algorithm_utils

INITIAL_DIV_NUM = 2

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


def remove_highest_precision_from_selections_in_hier(change_set):
    for com in range(0, len(change_set)):
        for c in change_set[com]:
            if len(c["search_types"]) > 0:
                c["search_types"].pop()  # remove current highest precision

# can only be used in searching period
def remove_unchangable(change_set):
    new_change_set = []
    for c in change_set:
        t = c["search_types"]
        if t != [] and c["type"] != t[-1]:
            new_change_set.append(c)
    return new_change_set

def remove_unchangable_from_hier(change_set):
    output_change_set = []
    com_change_set = []
    for com in range(0, len(change_set)):
        for c in change_set[com]:
            t = c["search_types"]
            if t != [] and c["type"] != t[-1]:
                com_change_set.append(c)
        output_change_set.append(com_change_set)
    return output_change_set

# can only be used in searching period
def get_unchangable_set(change_set):
    unchangbale_set = []
    for c in change_set:
        t = c["search_types"]
        if t == [] or c["type"] == t[-1]:
            unchangbale_set.append(c)
    return unchangbale_set


def get_unchangable_set_from_hier(change_set):
    unchangbale_set = []
    for com in range(0, len(change_set)):
        for c in change_set[com]:
            t = c["search_types"]
            if t == [] or c["type"] == t[-1]:
                unchangbale_set.append(c)
    return unchangbale_set

def regroup_by_change_set(change_set, new_change_set):
    # 将 new_change_set 转为集合以便快速查找
    new_change_set_ids = set()
    for item in new_change_set:
        if 'func' in item:
            key = (item['func'], item['name'], item['fvar_index'])
        elif 'caller' in item:
            key = (item['caller'], item['type'], item['fcall_index'])
        else:
            key = id(item)  # 备用方案
        new_change_set_ids.add(key)
    # 重新分组
    regrouped = []
    for sublist in change_set:
        filtered_sublist = []
        for item in sublist:
            if 'func' in item:
                item_key = (item['func'], item['name'], item['fvar_index'])
            elif 'caller' in item:
                item_key = (item['caller'], item['type'], item['fcall_index'])
            else:
                item_key = id(item)
            if item_key in new_change_set_ids:
                filtered_sublist.append(item)
        if filtered_sublist:  # 只添加非空子列表
            regrouped.append(filtered_sublist)
    return regrouped

def search_hierarchical_change_set(bitcode, change_set, current_config, div_num):
    print("get into search_hierarchical_change_set")
    delta_change_set = []
    complement_delta_change_set = []
    # flatten change_set[]
    flatten_change_set = []
    flatten_change_set = [item for sublist in change_set for item in sublist]    
    # divide change set and type set into div_num parts
    # and calculate each part's complement part
    bias = 0
    for com in range(0,len(change_set)):
        div_size = int(math.ceil(float(len(change_set[com]))/float(div_num)))
        for i in range(0, len(change_set[com]), div_size):
            start = i
            end = min(i + div_size, len(change_set[com]))  # Preventing out-of-bounds
            delta_change_set.append(change_set[com][start:end])
            complement_delta_change_set.append(flatten_change_set[:(start+bias)] + flatten_change_set[end+bias:])
        bias += len(change_set[com])
    # iterate each delta change and its complement delta change 
    # to find the current minimal change set
    new_change_set = []
    new_div_num = div_num
    have_better_set = False
    print(f'delta size: {len(delta_change_set)}, complement delta size: {len(complement_delta_change_set)}')

    for i in range(len(delta_change_set)):
        delta_change = delta_change_set[i]
        complement_delta_change = complement_delta_change_set[i]
        change_to_2nd_highest_precision(flatten_change_set)
        change_to_highest_precision(delta_change)
        new_config = config_utils.rebuild(flatten_change_set, current_config)
        if algorithm_utils.run_config(bitcode, new_config, flatten_change_set) and data_utils.is_nearly_equal(transform.current_run_result, data_utils.ORIGINAL_RESULT, config_utils.thres) and transform.current_run_time < algorithm_utils.min_time:
            algorithm_utils.min_time = transform.current_run_time
            new_change_set = delta_change
            new_div_num = INITIAL_DIV_NUM
            have_better_set = True
            print('delta better')
            algorithm_utils.update_opt(new_config)

        change_to_2nd_highest_precision(flatten_change_set)
        change_to_highest_precision(complement_delta_change)
        new_config = config_utils.rebuild(flatten_change_set, current_config)
        if algorithm_utils.run_config(bitcode, new_config, flatten_change_set) and data_utils.is_nearly_equal(transform.current_run_result, data_utils.ORIGINAL_RESULT, config_utils.thres) and transform.current_run_time < algorithm_utils.min_time:
            algorithm_utils.min_time = transform.current_run_time
            new_change_set = complement_delta_change
            new_div_num = div_num  # to optimize performance
            have_better_set = True
            print('comp_delta better')
            algorithm_utils.update_opt(new_config)

    if have_better_set:
        change_to_2nd_highest_precision(flatten_change_set)
        change_to_highest_precision(new_change_set)
        new_config = config_utils.rebuild(flatten_change_set, current_config)
        div_num = new_div_num
        change_set = regroup_by_change_set(change_set, new_change_set)
        if len(new_change_set) <= 1: return
    else:
        print("none better in current iteration")
        if div_num >= len(flatten_change_set):
            change_to_highest_precision(flatten_change_set)
            return
        else:
            div_num *= 2
    search_hierarchical_change_set(bitcode, change_set, current_config, div_num)

def hier_is_type_empty(change_set):
    for com in range(0, len(change_set)):
        for change in change_set[com]:
            if len(change["search_types"]) > 0:
                return False
    return True

def group_change_set_by_partition(change_set, partition):
    # 将 partition 转换为更易查询的字典形式
    partition_dict = partition
    # 用于存储分组结果的字典，键是组号，值是项列表
    grouped = {}
    # 用于跟踪已处理的项
    processed = set()
    # step1：遍历 change_set，按照 partition 分组
    for item in change_set:
        if not isinstance(item, dict):
            print(f"Warning: Skipping invalid item (not a dict): {item}")
            continue
        if 'func' in item:  # 变量的情况
            key = f"{item['func']}.{item['name']}"
        elif 'caller' in item:  # 函数调用的情况
            key = f"{item['caller']}.{item['type']}"
        else:
            key = None  # 不应该发生，但作为保护
        if key and key in partition_dict:
            group_id = partition_dict[key]
            if group_id not in grouped:
                grouped[group_id] = []
            grouped[group_id].append(item)
            processed.add(id(item))  # 使用对象 ID 标记已处理
    # step2：处理未分组的项（单独分组）
    max_group_id = max(grouped.keys(), default=-1) if grouped else -1
    for item in change_set:
        if id(item) not in processed:
            max_group_id += 1
            grouped[max_group_id] = [item]
    # 第三步：按 search_types 分离组内项
    final_grouped = {}
    current_group_id = 0
    for group_id, items in grouped.items():
        # 按 search_types 分组
        search_types_groups = {}
        for item in items:
            # 将 search_types 转为元组以便比较（列表不可哈希）
            search_types_key = tuple(item.get('search_types', ()))  # 转为元组，默认空元组
            if search_types_key not in search_types_groups:
                search_types_groups[search_types_key] = []
            search_types_groups[search_types_key].append(item)
        
        # 为每个 search_types 子组分配新组号
        for search_types, sub_items in search_types_groups.items():
            final_grouped[current_group_id] = sub_items
            current_group_id += 1

    # 将分组结果转为嵌套列表
    change_set_grouped = [final_grouped[group_id] for group_id in sorted(final_grouped.keys())]
    return change_set_grouped

def run(bitcode, change_set, partition_tree):
    new_conf = algorithm_utils.original_conf
    # searching until every search types is empty
    level = len(partition_tree)-1
    hichange_set = change_set
    
    while level>=0:
        hichange_set = group_change_set_by_partition(change_set, partition_tree[level])
        # print("hichange_set",hichange_set)
        while not hier_is_type_empty(hichange_set):
            # search which need highest precision & try to lower others' precision
            search_hierarchical_change_set(bitcode, hichange_set, new_conf, INITIAL_DIV_NUM)
            # rebuild the new config from new change set
            new_conf = config_utils.rebuild(get_unchangable_set_from_hier(hichange_set), new_conf)
            # remove highest precision
            hichange_set = remove_unchangable_from_hier(hichange_set)
            # remove the highest precision from the search types of the rest
            remove_highest_precision_from_selections_in_hier(hichange_set)
        level -= 1
    return new_conf

