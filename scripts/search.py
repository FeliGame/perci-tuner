#!/usr/bin/env python3

# @Author: LinLanfeng, Guocheng —— NUDT

import sys, math, json, os, shutil
import config_utils, data_utils, transform

# the initial divide number for delta debugging change set
INITIAL_DIV_NUM= 2
# the optimized bitcode & config file prefix
OPT_PREFIX = "optimized_"

global run_times
run_times = 0
global min_time
min_time = float('inf')
global thres

global original_conf
runned_change_set = set()

global bitcode_name # the original bitcode's filename


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
        if t != [] and c["type"] != t[-1]:
            new_change_set.append(c)
    return new_change_set


# can only be used in searching period
def get_unchangable_set(change_set):
    unchangbale_set = []
    for c in change_set:
        t = c["search_types"]
        if t == [] or c["type"] == t[-1]:
            unchangbale_set.append(c)
    return unchangbale_set


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


# run the current config and get the result
def run_config(bitcode, current_config, change_set):
    global run_times, original_conf, runned_change_set
    
    # check if the config has been runned
    key = json.dumps(change_set, sort_keys=False)
    if key in runned_change_set: return
    runned_change_set.add(key)

    run_times += 1

    # Initialize diff as an empty dictionary
    diff = {}

    # Compare current_config with original_conf and store differences in diff
    compare_json(original_conf, current_config, diff)

    result = transform.transform(bitcode, current_config, diff)
    if result == -1:
      print(">>> Perci-Tuner(Py) Error: running LLVM passes failed!")
      sys.exit(1)
    elif result == -2:
      print(">>> Perci-Tuner(Py) Error: running modified bitcode failed!")
      sys.exit(1)
    elif result == -3:
      print(">>> Perci-Tuner(Py) Error: the running time is not found!")
      sys.exit(1)
    elif result == -4:
      print(">>> Perci-Tuner(Py) Error: the running result is not found!")
      sys.exit(1)

    return result


# update the best ir code file
def update_opt_bc():
    global OPT_PREFIX, bitcode_name
    shutil.copyfile(transform.TEMP_BC_PATH, OPT_PREFIX + bitcode_name)


# using delta debugging algorithm to find the minimal change set
def search_minimal_change_set(bitcode, change_set, current_config, div_num, old_result):
    global min_time
    global thres
    delta_change_set = []
    complement_delta_change_set = []
    # calculate the size of each part
    div_size = int(math.ceil(float(len(change_set))/float(div_num)))
    print(f'\nchange set size: {len(change_set)}, div num: {div_num}, div size: {div_size}')
    # divide change set and type set into div_num parts
    # and calculate each part's complement part
    # Lin's version
    # for i in range(div_num-1):
    #     start = i * div_size
    #     end = (i+1) * div_size
    #     delta_change_set.append(change_set[start:end])
    #     complement_delta_change_set.append(change_set[:start] + change_set[end:])
    # delta_change_set.append(change_set[(div_num-1)*div_size:])
    # complement_delta_change_set.append(change_set[:(div_num-1)*div_size])

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

    for i in range(len(delta_change_set)):
        delta_change = delta_change_set[i]
        complement_delta_change = complement_delta_change_set[i]

        # if len(delta_change) > 0:
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(delta_change)
        new_config = config_utils.rebuild(change_set, current_config)
        if run_config(bitcode, new_config, change_set) and data_utils.is_nearly_equal(transform.current_run_result, old_result, thres) and transform.current_run_time < min_time:
            min_time = transform.current_run_time
            new_change_set = delta_change
            new_div_num = INITIAL_DIV_NUM
            have_better_set = True
            print('delta better')
            update_opt_bc()
        
        # if len(complement_delta_change) > 0:
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(complement_delta_change)
        new_config = config_utils.rebuild(change_set, current_config)
        if run_config(bitcode, new_config, change_set) and data_utils.is_nearly_equal(transform.current_run_result, old_result, thres) and transform.current_run_time < min_time:
            min_time = transform.current_run_time
            new_change_set = complement_delta_change
            new_div_num = div_num - 1  # to optimize performance
            have_better_set = True
            print('comp_delta better')
            update_opt_bc()

    if have_better_set:
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(new_change_set)
        new_config = config_utils.rebuild(change_set, current_config)
        div_num = new_div_num
        change_set = new_change_set
        if len(change_set) <= 1: return
    else:
        print("none better in current iteration")
        if div_num >= len(change_set):
            change_to_highest_precision(change_set)  # to be removed in 'remove_unchangable'
            return
        else:
            div_num *= 2
    search_minimal_change_set(bitcode, change_set, current_config, div_num, old_result)

# is every search types empty
def is_type_empty(change_set):
    for change in change_set:
        if len(change["search_types"]) > 0:
            return False
    return True

# search the new config
def search_conf(bitcode, change_set, original_result):
    global original_conf
    new_conf= original_conf
    # searching until every search types is empty
    while not is_type_empty(change_set):
        # search which need highest precision & try to lower others' precision
        search_minimal_change_set(bitcode, change_set, new_conf, INITIAL_DIV_NUM, original_result)
        # rebuild the new config from new change set
        new_conf = config_utils.rebuild(get_unchangable_set(change_set), new_conf)
        # remove highest precision
        change_set = remove_unchangable(change_set)
        # remove the highest precision from the search types of the rest
        remove_highest_precision_from_selections(change_set)
        # run_config(bitcode, new_conf)
        # time = transform.current_run_time
    return new_conf


def main():
    global run_times, min_time, thres, original_conf, bitcode_name
    if len(sys.argv) != 6:
        print("Usage: python search.py <bitcode> <conf_file> <relative_threshold, e.g. 1e-6> <run times per iteration><show history>")
        sys.exit(1)
    bitcode = sys.argv[1]
    original_conf_file = sys.argv[2]
    # relative threshold for the result
    thres = sys.argv[3]
    transform.RUNNING_TIMES = int(sys.argv[4])
    transform.show_history = sys.argv[5].lower() in ('True', 'true', '1', 'yes')

    data_utils.delete_data()

    # load the original config from file
    original_conf = json.loads(open(original_conf_file, 'r').read())

    # change_set keep those need to change precision and available precisions
    change_set = config_utils.parse_config(original_conf)
    changes_set_len = len(change_set)

    # get the original time and original result for compare
    # change_to_highest_precision(change_set)
    run_config(bitcode, original_conf, change_set)
    original_time = transform.current_run_time
    original_result = transform.current_run_result
    print("original time:", original_time, "original result:", original_result)
    
    # start searching
    print(">>> Perci-Tuner(Py): Running delta-debugging algorithm searching for valid configuration ...")

    # searching until all search types is empty
    min_time = original_time
    bitcode_name = os.path.basename(bitcode)
    new_conf = search_conf(bitcode, change_set, original_result)
    config_utils.print_config(new_conf, OPT_PREFIX + bitcode_name + ".json")
    
    print(">>> Perci-Tuner(Py): Running delta-debugging algorithm finished! Run times:", run_times, "Change set len:", changes_set_len)
    print(">>> Perci-Tuner(Py): Config: " + OPT_PREFIX + bitcode_name + ".json", "IR:", OPT_PREFIX + bitcode_name, "Optimized_time:", min_time, "Original_time:", original_time)


if __name__ == "__main__":
  main()