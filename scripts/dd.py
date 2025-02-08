#!/usr/bin/env python3

# @Author: LinLanfeng, Guocheng —— NUDT

import sys, math, json, os
import config_utils, data_utils, transform

# the initial divide number for delta debugging change set
INITIAL_DIV_NUM= 2

global run_times
run_times = 0
global min_time
min_time = float('inf')
global thres

global original_var_conf


# change each element's type to the highest precision in change set
def change_to_highest_precision(change_set):
    for i in range(0, len(change_set)):
        c = change_set[i]
        t = c["search_types"]
        if len(t) > 0:
            c["type"] = t[-1]

# change each element's type to the 2nd highest precision in change set
def change_to_2nd_highest_precision(change_set):
    for i in range(0, len(change_set)):
        c = change_set[i]
        t = c["search_types"]
        if len(t) > 1:
            c["type"] = t[-2]

# remove the highest precision from the search types of the variables
def remove_highest_precision_from_selections(change_set):
    for i in range(0, len(change_set)):
        c= change_set[i]
        t= c["search_types"]
        if len(t) > 0:
            c["search_types"] = t[:-1]

# remove elements in change_set whose type is already confirmed
# can only be used in searching period
def remove_confirmed(change_set):
    new_change_set = []
    for c in change_set:
        t = c["search_types"]
        if t != [] and c["type"] != t[-1]:
            new_change_set.append(c)
    return new_change_set

# remove elements in change_set whose type is already confirmed
# can only be used in searching period
def get_confirmed_change_set(change_set):
    confirmed_change_set = []
    for i in range(0, len(change_set)):
        c = change_set[i]
        t = c["search_types"]
        if t == [] or c["type"] == t[-1]:
            confirmed_change_set.append(c)
    return confirmed_change_set

def compare_json(original, current, diff, prefix=""):
    """
    Recursively compare two JSON objects and store differences in diff.
    :param original: The original JSON object.
    :param current: The current JSON object.
    :param diff: The dictionary to store differences.
    :param prefix: The prefix for nested keys.
    """
    if isinstance(original, dict) and isinstance(current, dict):
        for key in original:
            if key not in current:
                diff[prefix + key] = f"{original[key]}->None"
            else:
                compare_json(original[key], current[key], diff, prefix + key + ".")
        for key in current:
            if key not in original:
                diff[prefix + key] = f"None->{current[key]}"
    elif isinstance(original, list) and isinstance(current, list):
        for i in range(max(len(original), len(current))):
            if i >= len(original):
                diff[prefix + str(i)] = f"None->{current[i]}"
            elif i >= len(current):
                diff[prefix + str(i)] = f"{original[i]}->None"
            else:
                compare_json(original[i], current[i], diff, prefix + str(i) + ".")
    else:
        if original != current:
            diff[prefix[:-1]] = f"{original}->{current}"

# run the current variable config and get the result
def run_config(bitcode, var_config):
  global run_times, original_var_conf
  run_times += 1

  # Initialize diff as an empty dictionary
  diff = {}

  # Compare var_config with original_var_conf and store differences in diff
  compare_json(original_var_conf, var_config, diff)

  # Convert diff to JSON format if needed
  diff_json = json.dumps(diff, indent=4)

  result = transform.transform(bitcode, var_config, diff)
  if result == -1:
    print(">>> Perci-Tuner Error: running LLVM passes failed!")
    sys.exit(1)
  elif result == -2:
    print(">>> Perci-Tuner Error: running modified bitcode failed!")
    sys.exit(1)
  elif result == -3:
    print(">>> Perci-Tuner Error: the running time is not found!")
    sys.exit(1)
  elif result == -4:
    print(">>> Perci-Tuner Error: the running result is not found!")
    sys.exit(1)

  return result

# using delta debugging algorithm to find the minimal change set
def search_minimal_change_set(bitcode, change_set, var_config, div_num, old_result):
    global min_time
    global thres
    delta_change_set = []
    complement_delta_change_set = []
    # calculate the size of each part
    dive_size = int(math.ceil(float(len(change_set))/float(div_num)))

    # divide change set and type set into div_num parts
    # and calculate each part's complement part
    for i in range(div_num-1):
        delta_change_set.append(change_set[i*dive_size:(i+1)*dive_size])
        complement_delta_change_set.append(change_set[:i*dive_size] + change_set[(i+1)*dive_size:])
    delta_change_set.append(change_set[(div_num-1)*dive_size:])
    complement_delta_change_set.append(change_set[:(div_num-1)*dive_size])
    
    # iterate each delta change and its complement delta change 
    # to find the current minimal change set
    new_change_set = []
    new_div_num = div_num
    have_better_set = False
    for i in range(div_num):
        delta_change = delta_change_set[i]
        complement_delta_change = complement_delta_change_set[i]

        if len(delta_change) > 0:
            change_to_2nd_highest_precision(change_set)
            change_to_highest_precision(delta_change)
            new_var_config = config_utils.rebuild(change_set, var_config)
            run_config(bitcode, new_var_config)
            if data_utils.is_nearly_equal(old_result, thres) and data_utils.get_current_time() < min_time:
                # if delta change set is better 
                # continue to search the delta change
                min_time = data_utils.get_current_time()
                new_change_set = delta_change
                new_div_num = INITIAL_DIV_NUM
                have_better_set = True
        
        if len(complement_delta_change) > 0:
            change_to_2nd_highest_precision(change_set)
            change_to_highest_precision(complement_delta_change)
            new_var_config = config_utils.rebuild(change_set, var_config)
            run_config(bitcode, new_var_config)

            if data_utils.is_nearly_equal(old_result, thres) and data_utils.get_current_time() < min_time:
                # if complement delta change set is better
                # continue to search the complement delta change
                min_time = data_utils.get_current_time()
                new_change_set = complement_delta_change
                new_div_num = div_num - 1
                have_better_set = True

    # if delta change or it's complement change is better
    # continue to search the current minimal change set
    if have_better_set:
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(new_change_set)
        new_var_config = config_utils.rebuild(change_set, var_config)
        if len(new_change_set) > 1:
            search_minimal_change_set(bitcode, new_change_set, new_var_config, new_div_num, old_result)
        return 
    
    # if delta change or it's complement change is not better
    # and the current div_num smaller than the change set's size
    # increase the div_num and continue to search the current minimal change set
    #
    # else if the current div_num is not smaller than the change set's size
    # the current change set is the minimal change set
    if div_num >= len(change_set):
        change_to_highest_precision(change_set)
    else:
        search_minimal_change_set(bitcode, change_set, var_config, div_num * 2, old_result)
    return


# is every variables's search types empty
def is_type_empty(change_set):
    for change in change_set:
        if len(change["search_types"]) > 0:
            return False
    return True

# search the new variable config
def search_conf(bitcode, change_set, original_var_conf, original_result):
    var_conf= original_var_conf

    print("Search_conf tell change_set: ", len(change_set))

    # searching until every variables's search types is empty
    while not is_type_empty(change_set):
        # search the variables need highest precision 
        search_minimal_change_set(bitcode, change_set, var_conf, INITIAL_DIV_NUM, original_result)
        # rebuild the new variable config from new change set
        var_conf = config_utils.rebuild(get_confirmed_change_set(change_set), var_conf)
        # remove the variables whose type is already confirmed
        # these variables that comfirmed to be highest precision
        change_set = remove_confirmed(change_set)
        # remove the highest precision from the search types of the rest variables
        remove_highest_precision_from_selections(change_set)
        run_config(bitcode, var_conf)
        time = data_utils.get_current_time()
    return var_conf


def main():
    global run_times, min_time, thres, original_var_conf
    if len(sys.argv) != 4:
        print("Usage: python dd2.py <bitcode> <conf_file> <threshold, e.g. 1e-6>")
        sys.exit(1)
    bitcode = sys.argv[1]
    original_var_conf_file = sys.argv[2]
    # the threshold for the result
    thres = sys.argv[3]

    data_utils.del_csv()

    # load the original config from file
    original_var_conf = json.loads(open(original_var_conf_file, 'r').read())

    # change_set keep var that need to change precision and available precisions
    change_set = config_utils.parse_var_conf(original_var_conf)
    changes_set_len = len(change_set)

    # get the original time and original result for compare
    # change_to_highest_precision(change_set)
    run_config(bitcode, original_var_conf)
    original_time = data_utils.get_current_time()
    original_result = data_utils.get_current_result()
    print("original time:", original_time, "original result:", original_result)
    
    # start searching
    print(">>> Perci-Tuner(Py): Running delta-debugging algorithm searching for valid configuration ...")

    # searching until every variables's search types is empty
    min_time = original_time
    new_var_conf = search_conf(bitcode, change_set, original_var_conf, original_result)
    bitcode_name =  os.path.basename(bitcode)
    config_utils.print_config(new_var_conf, "dd_valid_" + bitcode_name + ".json")
    
    print(">>> Perci-Tuner(Py): Running delta-debugging algorithm finished! Run times:", run_times, "Change set len:", changes_set_len)
    print(">>> Perci-Tuner(Py): Config: dd_valid_" + bitcode_name + ".json", "Optimized_time:", min_time, "Original_time:", original_time)

if __name__ == "__main__":
  main()