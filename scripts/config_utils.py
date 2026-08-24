#!/usr/bin/env python3
# Handle config.json & change_set

# @Author: LinLanfeng, Guocheng —— NUDT

import copy, json, re, random

global thres
global CURRENT_CONFIG
global CURRENT_CONFIG_PARENT
CURRENT_CONFIG_PARENT = 1

# valid precision types
PRECISIONS = ["__fp16", "float", "double", "long double"]
CONST_PRECISIONS = ["const " + p for p in PRECISIONS]
POINTER_PRECISIONS = [p + "*" for p in PRECISIONS]
# Same as ChangeMathCall.h
FUNCTION_PRECISIONS = [
    # 三角函数
    "sin", "sinf", "sinl", "cos", "cosf", "cosl", "tan", "tanf", "tanl",
    "asin", "asinf", "asinl", "acos", "acosf", "acosl", "atan", "atanf", "atanl",

    # 双曲函数
    "sinh", "sinhf", "sinhl", "cosh", "coshf", "coshl", "tanh", "tanhf", "tanhl",

    # 指数对数
    "exp", "expf", "expl", "log", "logf", "logl", 
    "exp2", "exp2f", "exp2l", "log2", "log2f", "log2l",
    "log10", "log10f", "log10l",

    # 幂和根
    "sqrt","sqrtf","sqrtl", "cbrt", "cbrtf", "cbrtl",
    "pow", "powf", "powl", "hypot", "hypotf", "hypotl",

    # 其他
    "fabs", "fabsf", "fabsl",        # 绝对值
    # "ceil", "ceilf", "ceill",       # 向上取整（llvm.ceil.xx）
    # "floor", "floorf", "floorl",     # 向下取整（llvm.floor.xx）
    "round", "roundf", "roundl",     # 四舍五入
    "fmod", "fmodf", "fmodl",       # 浮点余数
    "atan2", "atan2f", "atan2l",    # 二参数反正切
    "erf", "erff", "erfl",          # 误差函数（C99）
    "tgamma", "tgammaf", "tgammal"  # gamma函数（C99）
]


# Helper function to extract the size from an (const) array type (e.g., "const float[6]" -> 6)
def extract_array_size(type_str):
    match = re.match(r"(.+)\[(\d+)\]", type_str)
    if match:
        return match.group(2)  # Return the size as a string
    return None


# fill search types for each variable & struct member variable
# and delete the variables that don't need to change precisions
def set_var_search_types(change_set):
    for change_var in change_set:
        type_str = change_var['type']
        size = extract_array_size(type_str)  # Extract the array size

        # 有range字段用range字段，但要确保顺序符合PRECISIONS
        if 'range' in change_var and change_var['range']:
            # 按PRECISIONS顺序过滤和排序range中的类型
            valid_range = []
            for p in PRECISIONS:
                for r in change_var['range']:
                    base_r = r.split('[')[0] if '[' in r else r
                    if base_r == p:
                        valid_range.append(r)
                        break
            
            if size:  # 处理数组类型
                base_range = [p.split('[')[0] if '[' in p else p for p in valid_range]
                change_var['search_types'] = [f"{p}[{size}]" for p in base_range]
            else:
                change_var['search_types'] = valid_range
            continue

        if size:  # Handle array types
            base_type = type_str.split('[')[0]
            if base_type in PRECISIONS:
                change_var['search_types'] = [f"{p}[{size}]" for p in PRECISIONS[:PRECISIONS.index(base_type) + 1]]
        elif type_str in PRECISIONS:
            change_var['search_types'] = PRECISIONS[:PRECISIONS.index(type_str) + 1]
        elif type_str in CONST_PRECISIONS:
            change_var['search_types'] = CONST_PRECISIONS[:CONST_PRECISIONS.index(type_str) + 1]
        elif type_str in POINTER_PRECISIONS:
            change_var['search_types'] = POINTER_PRECISIONS[:POINTER_PRECISIONS.index(type_str) + 1]

    # Delete the variables that don't need to change precisions
    # i.  the variables' type is not in the valid precision types
    # ii. the variables' 'search_types' only have one option for the variable
    return [change_var for change_var in change_set if len(change_var.get('search_types', [])) > 1]


def _process_variable_list(var_list, item_type, index_name, shuffle=False, filter_func=None):
    change_entries = []
    if var_list is None:
        return change_entries
        
    indices = list(range(len(var_list)))
    if shuffle:
        random.shuffle(indices)
        
    for i in indices:
        item = var_list[i]
        if filter_func and not filter_func(item):
            continue
            
        change_entry = copy.deepcopy(item)
        change_entry[index_name] = i
        if item_type:
            change_entry[item_type] = True if item_type == 'global' else item_type
        change_entry['search_types'] = []
        change_entries.append(change_entry)
    
    return change_entries

def _process_nested_variable_list(nested_list, item_type, index_name, shuffle=False, filter_func=None):
    change_entries = []
    if nested_list is None:
        return change_entries
        
    outer_list = list(nested_list.items())
    if shuffle:
        random.shuffle(outer_list)
        
    for outer_key, var_list in outer_list:
        if var_list is None:
            continue
            
        entries = _process_variable_list(var_list, outer_key, index_name, shuffle, filter_func)
        for entry in entries:
            entry[item_type] = outer_key
        change_entries.extend(entries)
    
    return change_entries

def parse_config(config, shuffled=False):
    global thres
    if 'result_error' in config and config['result_error'] is not None:
        thres = float(config['result_error'])
    change_set = []
    
    # Define common filter function
    def precision_filter(item):
        return any(p in item['type'] for p in PRECISIONS)
    
    # Process global variables
    global_vars = config.get('global')
    change_set.extend(_process_variable_list(global_vars, 'global', 'gvar_index', shuffled, precision_filter))
    
    # Process local variables
    local_vars = config.get('local')
    change_set.extend(_process_nested_variable_list(local_vars, 'func', 'fvar_index', shuffled, precision_filter))

    # Process struct members
    structs = config.get('structs')
    change_set.extend(_process_nested_variable_list(structs, 'struct', None, shuffled, precision_filter))

    # 处理函数调用 (保持原有结构)
    calls = config.get('calls')
    if calls is not None:
        caller_list = list(calls.items())
        if shuffled:
            random.shuffle(caller_list)
            
        for caller, call_insts in caller_list:
            if call_insts is None:
                continue
            call_indices = list(range(len(call_insts)))
            if shuffled:
                random.shuffle(call_indices)
                
            for i in call_indices:
                change_entry = copy.deepcopy(call_insts[i])
                func_name = change_entry['type']
                if func_name not in FUNCTION_PRECISIONS:
                    continue
        
                precision_suffix = None 
                # Handle funcs end with f, l
                if func_name.startswith('ceil'):
                    base_name = 'ceil'
                    precision_suffix = func_name[4:]
                elif func_name.startswith('erf'):
                    base_name = 'erf'
                    precision_suffix = func_name[3:]
                else:  # Handle others
                    if func_name.endswith('f'):
                        precision_suffix = 'f'
                    elif func_name.endswith('l'):
                        precision_suffix = 'l' 
                # set max_level by precision_suffix        
                if precision_suffix == 'f':
                    max_level = 0  # float
                elif precision_suffix == 'l':
                    max_level = 2  # long double
                else:
                    max_level = 1  # double
                # set base_name by precision_suffix
                base_name = func_name[:-1] if precision_suffix else func_name
        
                precision_versions = [
                    base_name + 'f',
                    base_name,
                    base_name + 'l'
                ][:max_level+1]  # Important slice!
                # 过滤掉f结尾的单精度函数版本（高置信场景）
                # precision_versions = [v for v in precision_versions if not v.endswith('f')]
                
                change_entry['fcall_index'] = i
                change_entry['caller'] = caller
                change_entry['search_types'] = precision_versions
                change_set.append(change_entry)

    change_set = set_var_search_types(change_set)
    return change_set


def _update_variable_type(var_list, index, new_type):
    if index < len(var_list):
        member_info = var_list[index]
        is_pointer = member_info['type'].endswith('*')
        final_type = new_type
        if is_pointer and not new_type.endswith('*'):
            final_type += '*'
        if member_info['type'] != final_type:
            member_info['type'] = final_type

# rebuild config from change set
def rebuild(change_set):
    new_config = copy.deepcopy(CURRENT_CONFIG)
    for item in change_set:
        if 'global' in item:
            _update_variable_type(new_config['global'], item['gvar_index'], item['type'])
            
        elif 'func' in item:
            func = item['func']
            local_vars = new_config['local'].get(func)
            if local_vars is not None:
                _update_variable_type(local_vars, item['fvar_index'], item['type'])
                        
        elif 'caller' in item:
            caller = item['caller']
            call_entries = new_config['calls'].get(caller)
            if call_entries is not None:
                _update_variable_type(call_entries, item['fcall_index'], item['type'])
        
        elif 'struct' in item:
            struct = item['struct']
            struct_members = new_config['structs'].get(struct)
            if struct_members is not None:
                _update_variable_type(struct_members, item['member_index'], item['type'])
       
    return new_config


# print config to file
def print_config(config, file_name):
    with open(file_name, "w") as f:
        json.dump(config, f, indent=4)
