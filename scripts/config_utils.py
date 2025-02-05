#!/usr/bin/env python3

# @Author: LinLanfeng —— NUDT

import copy, json

# valid precision types
PRECISIONS = ["float", "double", "long double"]
CONST_PRECISIONS = ["const "+ p for p in PRECISIONS]
POINTER_PRECISIONS = [p+"*" for p in PRECISIONS]


# fill search types for each variable
# and delete the variables that dont need to change precicions
def parse_search_var_conf(change_set):
  for change_var in change_set:
    if change_var['type'] in PRECISIONS:
      change_var['search_types'] = PRECISIONS[:PRECISIONS.index(change_var['type'])+1]
    elif change_var['type'] in CONST_PRECISIONS:
      change_var['search_types'] = CONST_PRECISIONS[:CONST_PRECISIONS.index(change_var['type'])+1]
    elif change_var['type'] in POINTER_PRECISIONS:
      change_var['search_types'] = POINTER_PRECISIONS[:POINTER_PRECISIONS.index(change_var['type'])+1]
  # delete the variables that dont need to change precicions
  # i.  the variables' type is not in the valid precision types
  # ii. the variables' 'search_types' only have one option for the variable
  return [change_var for change_var in change_set if len(change_var['search_types']) > 1]

# parse variable config file to change set
def parse_var_conf(var_conf):
  change_set = []
  # TODO: put global into change_set
  for func, func_vars in var_conf['local'].items():
    if func_vars is None:
      continue
    for i in range(0, len(func_vars)):
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