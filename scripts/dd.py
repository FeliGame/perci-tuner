#!/usr/bin/env python3

# @Author: LinLanfeng —— NUDT

import sys, math, json, os
import config_utils, data_utils, transform

# the initial divide number for delta debugging change set
INITIAL_DIV_NUM= 2

global run_times
run_times = 0
global min_score
min_score = 0
global thres


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

# run the current variable config and get the result
def run_config(bitcode, var_config):
  global run_times
  run_times += 1

  result = transform.transform(bitcode, var_config)
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
def search_minimal_change_set(bitcode, change_set, var_config, div_num, old_score, old_result):
    global min_score
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
            if data_utils.is_nearly_equal(old_result, thres) and data_utils.get_current_score() < old_score:
                # if delta change set is better 
                # continue to search the delta change
                old_score = data_utils.get_current_score()
                new_change_set = delta_change
                new_div_num = INITIAL_DIV_NUM
                have_better_set = True
                min_score = old_score
        
        if len(complement_delta_change) > 0:
            change_to_2nd_highest_precision(change_set)
            change_to_highest_precision(complement_delta_change)
            new_var_config = config_utils.rebuild(change_set, var_config)
            run_config(bitcode, new_var_config)

            if data_utils.is_nearly_equal(old_result, thres) and data_utils.get_current_score() < old_score:
                # if complement delta change set is better
                # continue to search the complement delta change
                old_score = data_utils.get_current_score()
                new_change_set = complement_delta_change
                new_div_num = div_num - 1
                have_better_set = True
                min_score = old_score

    # if delta change or it's complement change is better
    # continue to search the current minimal change set
    if have_better_set:
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(new_change_set)
        new_var_config = config_utils.rebuild(change_set, var_config)
        if len(new_change_set) > 1:
            search_minimal_change_set(bitcode, new_change_set, new_var_config, new_div_num, old_score, old_result)
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
        search_minimal_change_set(bitcode, change_set, var_config, div_num * 2, old_score, old_result)
    return


# is every variables's search types empty
def is_type_empty(change_set):
    for change in change_set:
        if len(change["search_types"]) > 0:
            return False
    return True

# search the new variable config
def search_conf(bitcode, change_set, original_var_conf, original_score, original_result):
    var_conf= original_var_conf
    score = original_score

    print("Search_conf tell change_set: ", len(change_set))

    # searching until every variables's search types is empty
    while not is_type_empty(change_set):
        # search the variables need highest precision 
        search_minimal_change_set(bitcode, change_set, var_conf, INITIAL_DIV_NUM, score, original_result)
        # rebuild the new variable config from new change set
        var_conf = config_utils.rebuild(get_confirmed_change_set(change_set), var_conf)
        # remove the variables whose type is already confirmed
        # these variables that comfirmed to be highest precision
        change_set = remove_confirmed(change_set)
        # remove the highest precision from the search types of the rest variables
        remove_highest_precision_from_selections(change_set)
        run_config(bitcode, var_conf)
        score = data_utils.get_current_score()
    return var_conf, score


def main():
    global run_times, min_score, thres
    if len(sys.argv) != 4:
        print("Usage: python dd2.py <bitcode> <conf_file>")
        sys.exit(1)
    bitcode = sys.argv[1]
    original_var_conf_file = sys.argv[2]
    # the threshold for the result
    thres = sys.argv[3]

    data_utils.del_csv()

    # load the original config from file
    original_var_conf = json.loads(open(original_var_conf_file, 'r').read())
    # the vector used to seperate the variables 
    change_set = []


    # convert the original config to a change set 
    # and remove the variables that dont need to change precicions
    change_set = config_utils.parse_var_conf(original_var_conf)
    changes_set_len = len(change_set)

    # start searching
    print(">>> Perci-Tuner: Running delta-debugging algorithm searching for valid configuration ...")

    # get the original score and original result
    change_to_highest_precision(change_set)
    run_config(bitcode, original_var_conf)
    original_score = data_utils.get_current_score()
    original_result = data_utils.get_current_result()


    # searching until every variables's search types is empty
    new_var_conf, _ = search_conf(bitcode, change_set, original_var_conf, original_score, original_result)
    bitcode_name =  os.path.basename(bitcode)
    config_utils.print_config(new_var_conf, "dd_valid_" + bitcode_name + ".json")
    
    print(">>> Perci-Tuner: Running delta-debugging algorithm finished! run times: ", run_times, " change set len:", changes_set_len)
    print(">>> Perci-Tuner Result: new config is in dd_valid_", bitcode_name, ".json, new_time: ", min_score, ", original_time: ", original_score)

if __name__ == "__main__":
  main()