#!/usr/bin/env python3

# @Author: LinLanfeng —— NUDT

import copy, json, re

# valid precision types
PRECISIONS = ["float", "double", "long double"]
CONST_PRECISIONS = ["const " + p for p in PRECISIONS]
POINTER_PRECISIONS = [p + "*" for p in PRECISIONS]
# Same as ChangeFunctionCall.h
FUNCTION_PRECISIONS = [
    "sin", "sinf", "sinl",
    "cos", "cosf", "cosl",
    "tan", "tanf", "tanl",
    "sqrt","sqrtf","sqrtl",
    "exp", "expf", "expl",
    "log", "logf", "logl"
]

# Helper function to extract the size from an (const) array type (e.g., "const float[6]" -> 6)
def extract_array_size(type_str):
    match = re.match(r"(.+)\[(\d+)\]", type_str)
    if match:
        return match.group(2)  # Return the size as a string
    return None

# fill search types for each variable
# and delete the variables that don't need to change precisions
def set_var_search_types(change_set):
    for change_var in change_set:
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
  for func, func_vars in config['local'].items():
    if func_vars is None:
      continue
    for i in range(0, len(func_vars)):
      # Skip pointer-type & non-floating-type (though they are shown in config.json)
      type_str = func_vars[i]['type']
      if (('struct' not in type_str and '*' in type_str) 
          or not any(p in type_str for p in PRECISIONS)):
         continue
      change_entry = copy.deepcopy(func_vars[i])
      change_entry['fvar_index'] = i
      change_entry['func'] = func
      change_entry['search_types'] = []
      change_set.append(change_entry)
  change_set = set_var_search_types(change_set)

  for caller, call_insts in config['calls'].items():
      if call_insts is None:
        continue
      for i in range(0, len(call_insts)):
        change_entry = copy.deepcopy(call_insts[i])
        func_name = change_entry['type']
        if func_name not in FUNCTION_PRECISIONS:
            continue
        # Remove precision suffix
        base_name = func_name.rstrip('fl') if func_name.endswith(('f', 'l')) else func_name
        if func_name.endswith('f'):
            max_level = 0  # float
        elif func_name.endswith('l'):
            max_level = 2  # long double
        else:
            max_level = 1  # double
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
        local_vars = new_config['local'].get(func)  # 添加临时变量
        if local_vars is not None:
            fvar_index = item['fvar_index']
            if fvar_index < len(local_vars):
                var_info = local_vars[fvar_index]  # 使用临时变量
                if var_info['type'] != item['type']:
                    var_info['type'] = item['type']
                    
    if 'caller' in item:
        caller = item['caller']
        call_entries = new_config['calls'].get(caller)  # 添加临时变量
        if call_entries is not None:
            fcall_index = item['fcall_index']
            if fcall_index < len(call_entries):
                callee_info = call_entries[fcall_index]  # 使用临时变量
                if callee_info['type'] != item['type']:
                    callee_info['type'] = item['type']
       
  return new_config

# print config to file
def print_config(config, file_name):
    with open(file_name, "w") as f:
        json.dump(config, f, indent=4)