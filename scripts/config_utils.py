#!/usr/bin/env python3
# Handle config.json & change_set

# @Author: LinLanfeng, Guocheng —— NUDT

import copy, json, re

# valid precision types
PRECISIONS = ["__fp16", "float", "double", "long double"]
CONST_PRECISIONS = ["const " + p for p in PRECISIONS]
POINTER_PRECISIONS = [p + "*" for p in PRECISIONS]
# Same as ChangeFunctionCall.h
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
        # 有range字段用range字段，否则用PRECISIONS指定的范围
        if 'range' in change_var and change_var['range']:
            change_var['search_types'] = change_var['range']
            continue
        
        type_str = change_var['type']
        size = extract_array_size(type_str)  # Extract the array size

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


def parse_config(config):
    change_set = []
    # TODO: put global into change_set
    local_vars = config.get('local')
    if local_vars is not None:
        for func, func_vars in config['local'].items():
            if func_vars is None:
                continue
            for i in range(0, len(func_vars)):
                # Skip pointer-type & non-floating-type (though they are shown in config.json)
                type_str = func_vars[i]['type']
                if (('*' in type_str) or not any(p in type_str for p in PRECISIONS)):
                    continue
                change_entry = copy.deepcopy(func_vars[i])
                change_entry['fvar_index'] = i
                change_entry['func'] = func
                change_entry['search_types'] = []
                change_set.append(change_entry)

    structs = config.get('structs')
    if structs is not None:
        for struct, members in structs.items():
            if members is None:
                continue
            for i in range(0, len(members)):
                # Skip pointer-type & non-floating-type (though they are shown in config.json)
                type_str = members[i]['type']
                if (('*' in type_str) or not any(p in type_str for p in PRECISIONS)):
                    continue
                change_entry = copy.deepcopy(members[i])
                change_entry['struct'] = struct
                change_entry['search_types'] = []
                change_set.append(change_entry)
    change_set = set_var_search_types(change_set)

    calls = config.get('calls')
    if calls is not None:
        for caller, call_insts in config['calls'].items():
            if call_insts is None:
                continue
            for i in range(0, len(call_insts)):
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
                change_entry['fcall_index'] = i
                change_entry['caller'] = caller
                change_entry['search_types'] = precision_versions
                change_set.append(change_entry)

    return change_set


# rebuild config from change set
def rebuild(change_set, config):
    new_config = copy.deepcopy(config)
    for item in change_set:
        if 'func' in item:
            func = item['func']
            local_vars = new_config['local'].get(func)
            if local_vars is not None:
                member_index = item['fvar_index']
                if member_index < len(local_vars):
                    member_info = local_vars[member_index]
                    if member_info['type'] != item['type']:
                        member_info['type'] = item['type']
                        
        elif 'caller' in item:
            caller = item['caller']
            call_entries = new_config['calls'].get(caller)
            if call_entries is not None:
                fcall_index = item['fcall_index']
                if fcall_index < len(call_entries):
                    callee_info = call_entries[fcall_index]
                    if callee_info['type'] != item['type']:
                        callee_info['type'] = item['type']
        
        elif 'struct' in item:
            struct = item['struct']
            struct_members = new_config['structs'].get(struct)
            if struct_members is not None:
                member_index = item['member_index']
                if member_index < len(struct_members):
                    member_info = struct_members[member_index]
                    if member_info['type'] != item['type']:
                        member_info['type'] = item['type']
       
    return new_config


# print config to file
def print_config(config, file_name):
    with open(file_name, "w") as f:
        json.dump(config, f, indent=4)