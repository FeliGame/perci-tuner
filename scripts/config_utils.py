#!/usr/bin/env python3

# @Author: LinLanfeng —— NUDT

import copy, json, re

# valid precision types
PRECISIONS = ["float", "double", "long double"]
CONST_PRECISIONS = ["const " + p for p in PRECISIONS]
POINTER_PRECISIONS = [p + "*" for p in PRECISIONS]

# Helper function to extract the size from an (const) array type (e.g., "const float[6]" -> 6)
def extract_array_size(type_str):
    match = re.match(r"(.+)\[(\d+)\]", type_str)
    if match:
        return match.group(2)  # Return the size as a string
    return None

# fill search types for each variable
# and delete the variables that don't need to change precisions
def parse_search_var_conf(change_set):
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

# parse variable config file to change set
def parse_var_conf(var_conf):
  change_set = []
  # TODO: put global into change_set
  for func, func_vars in var_conf['local'].items():
    if func_vars is None:
      continue
    for i in range(0, len(func_vars)):
      # Skip pointer-type & non-floating-type (though they are shown in config.json)
      type_str = func_vars[i]['type']
      if (('struct' not in type_str and '*' in type_str) 
          or not any(p in type_str for p in PRECISIONS)):
         continue
      change_set_vars = copy.deepcopy(func_vars[i])
      change_set_vars['fvar_index'] = i
      change_set_vars['func'] = func
      change_set_vars['search_types'] = []
      change_set.append(change_set_vars)
  return parse_search_var_conf(change_set)

# rebuild variable config from change set
def rebuild(change_set, var_config):
  new_var_config = copy.deepcopy(var_config)
  for change_var in change_set:
    if new_var_config['local'][change_var['func']] != None:
      var_info = new_var_config['local'][change_var['func']][change_var['fvar_index']]
      if  var_info['type'] != change_var['type']:
        var_info['type'] = change_var['type']
  return new_var_config

# print variable config to file
def print_config(var_config, file_name):
    with open(file_name, "w") as f:
        json.dump(var_config, f, indent=4)