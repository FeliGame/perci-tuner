#!/usr/bin/env python3

# @Author: LinLanfeng, Guocheng —— NUDT

import sys, math, json, os, shutil, time, copy
import config_utils, data_utils, transform

# the initial divide number for delta debugging change set
INITIAL_DIV_NUM = 1
# the optimized bitcode & config file prefix
OPT_PREFIX = "optimized_"

global run_times
run_times = 0
global min_time
min_time = float('inf')

global original_conf
global original_result
original_result = -1
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
def run_config(bitcode, current_config, change_set, skip_runned=True):
    global run_times, original_conf, runned_change_set
    
    # check if the config has been runned
    key = json.dumps(change_set, sort_keys=False)
    if skip_runned and key in runned_change_set: 
        print('>>> Perci-Tuner(Py): The config has been runned, skipped...')
        return
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
        print(">>> Perci-Tuner(Py) Warning: running IR failed, ignored...")
        # sys.exit(1)
        return False
    elif result == -3:
        print(">>> Perci-Tuner(Py) Error: the running time is not found!")
        sys.exit(1)
    elif result == -4:
        print(">>> Perci-Tuner(Py) Error: the running result is not found!")
        sys.exit(1)

    # save config if satisfy error threshold
    global original_result
    if original_result == -1: return True
    if data_utils.is_nearly_equal(transform.current_run_result, original_result, config_utils.thres):
        time_str = "{:.2e}".format(transform.current_run_time).replace('.', '_')
        filename = f"VALID_{time_str}.json"
        shutil.copyfile("temp_config.json", filename)
        print(f">>> Perci-Tuner(Py): Found runnable config, saved as {filename}")

    return True


# update the best IR and config file
def update_opt(opt_config):
    global OPT_PREFIX, bitcode_name
    shutil.copyfile(transform.TEMP_BC_PATH, OPT_PREFIX + bitcode_name)
    config_utils.print_config(opt_config, OPT_PREFIX + "config.json")


# using delta debugging algorithm to find the minimal change set
def search_minimal_change_set(bitcode, change_set, current_config, div_num):
    global min_time
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
        if run_config(bitcode, new_config, change_set) and data_utils.is_nearly_equal(transform.current_run_result, original_result, config_utils.thres) and transform.current_run_time < min_time:
            min_time = transform.current_run_time
            new_change_set = delta_change
            new_div_num = INITIAL_DIV_NUM
            have_better_set = True
            print('delta better')
            update_opt(new_config)
        
        # if len(complement_delta_change) > 0:
        change_to_2nd_highest_precision(change_set)
        change_to_highest_precision(complement_delta_change)
        new_config = config_utils.rebuild(change_set, current_config)
        if run_config(bitcode, new_config, change_set) and data_utils.is_nearly_equal(transform.current_run_result, original_result, config_utils.thres) and transform.current_run_time < min_time:
            min_time = transform.current_run_time
            new_change_set = complement_delta_change
            new_div_num = div_num - 1  # to optimize performance
            have_better_set = True
            print('comp_delta better')
            update_opt(new_config)

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
    search_minimal_change_set(bitcode, change_set, current_config, div_num)

# is every search types empty
def is_type_empty(change_set):
    for change in change_set:
        if len(change["search_types"]) > 0:
            return False
    return True

# search the new config
def search_conf_delta(bitcode, change_set):
    global original_conf
    new_conf= original_conf
    # searching until every search types is empty
    while not is_type_empty(change_set):
        # search which need highest precision & try to lower others' precision
        search_minimal_change_set(bitcode, change_set, new_conf, INITIAL_DIV_NUM)
        # rebuild the new config from new change set
        new_conf = config_utils.rebuild(get_unchangable_set(change_set), new_conf)
        # remove highest precision
        change_set = remove_unchangable(change_set)
        # remove the highest precision from the search types of the rest
        remove_highest_precision_from_selections(change_set)
        # run_config(bitcode, new_conf)
        # time = transform.current_run_time
    return new_conf

def exhaustive_search(bitcode, change_set, current_config):
    global min_time
    candidates = []
    
    # generate all combinations
    type_combinations = [[]]
    for change in change_set:
        if not change["search_types"]:
            continue
        new_combinations = []
        for t in type_combinations:
            for st in change["search_types"]:
                new_comb = t.copy()
                new_comb.append(st)
                new_combinations.append(new_comb)
        type_combinations = new_combinations
    
    for combination in type_combinations:
        # apply combination
        temp_change_set = []
        for i, change in enumerate(change_set):
            if not change["search_types"]:
                temp_change_set.append(change)
                continue
            new_change = change.copy()
            new_change["type"] = combination[i]
            temp_change_set.append(new_change)
        
        new_config = config_utils.rebuild(temp_change_set, current_config)
        
        if run_config(bitcode, new_config, temp_change_set):
            if (data_utils.is_nearly_equal(transform.current_run_result, original_result, config_utils.thres) and 
                transform.current_run_time < min_time):
                min_time = transform.current_run_time
                update_opt(new_config)
                candidates.append((new_config, min_time))

    return candidates

def search_conf_exhaustive(bitcode, change_set):
    candidates = exhaustive_search(bitcode, change_set, original_conf)
    return candidates[0][0] if candidates else original_conf

def parse_arguments():
    params = {
        '-ir': 'output.bc',
        '-config': 'config.json',
        '-thres': 1e-6,
        '-repeat': 1,
        '-history': True,
        '-algorithm': 'dd'
    }
    
    for arg in sys.argv[1:]:
        if '=' in arg:
            key, value = arg.split('=', 1)
            if key in params:
                # type cast
                if key == '-thres':
                    params[key] = float(value)
                elif key == '-repeat':
                    params[key] = int(value)
                elif key == '-history':
                    params[key] = value.lower() in ('true', '1', 'yes')
                else:
                    params[key] = value
        
    return params

def baseline_test(bitcode, original_conf, original_time, original_result):
    """运行三种精度的基准测试并输出配置文件"""
    print("\n>>> Baseline Test Results:")
    precision_types = [
        ("long double", "long_double.json"),
        ("double", "double.json"), 
        ("float", "float.json"),
        ("__fp16", "fp16.json")
    ]
    
    for prec, filename in precision_types:
        bench_conf = copy.deepcopy(original_conf)
        
        # 修改局部变量精度
        
        for func in bench_conf.get('local', {}):
            if bench_conf['local'][func]:
                for var in bench_conf['local'][func]:
                    if any(p in var['type'] for p in config_utils.PRECISIONS):
                        var['type'] = prec
        
        # # 修改结构体成员精度
        if bench_conf['structs']:
            for struct in bench_conf.get('structs', {}):
                if bench_conf['structs'][struct]:
                    for member in bench_conf['structs'][struct]:
                        if any(p in member['type'] for p in config_utils.PRECISIONS):
                            member['type'] = prec
        
        # 生成.json
        config_utils.print_config(bench_conf, filename)
        
        # 运行测试配置
        run_config(bitcode, bench_conf, [], skip_runned=False)
        bench_time = transform.current_run_time
        bench_result = transform.current_run_result
        
        # 计算结果指标
        speedup = (original_time - bench_time) / bench_time * 100
        error = abs((float(bench_result) - float(original_result)) / float(original_result))
        print(f"{prec:11} | Time: {bench_time:.2e}cycles | "
              f"Speedup: {speedup:6.2f}% | Relative Error: {error:.2e} | Config: {filename}")


def main():
    global run_times, min_time, original_conf, original_result, bitcode_name
    
    args = parse_arguments()

    bitcode = args['-ir']
    original_conf_file = args['-config']
    config_utils.thres = args['-thres']
    transform.RUNNING_TIMES = args['-repeat']
    transform.show_history = args['-history']
    algorithm = args['-algorithm']

    start_time = time.perf_counter()
    data_utils.delete_data()

    # load the original config from file
    original_conf = json.loads(open(original_conf_file, 'r').read())

    # change_set keep those need to change precision and available precisions
    change_set = config_utils.parse_config(original_conf)
    changes_set_len = len(change_set)

    print(f"  IR file: {args['-ir']}")
    print(f"  Config file: {args['-config']}")
    print(f"  Threshold: {config_utils.thres}")
    print(f"  Repeat times: {args['-repeat']}")
    print(f"  Show history: {args['-history']}")
    print(f"  Algorithm: {args['-algorithm']}")

    # get the original time and original result for compare
    # change_to_highest_precision(change_set)
    run_config(bitcode, original_conf, change_set)
    original_time = transform.current_run_time
    original_result = transform.current_run_result
    print("original time:", original_time, "original result:", original_result)
    
    # start searching

    # searching until all search types is empty
    min_time = original_time
    bitcode_name = os.path.basename(bitcode)
    if algorithm.lower() == 'dd':
        new_conf = search_conf_delta(bitcode, change_set)
    elif algorithm.lower() == 'exhaust':
        new_conf = search_conf_exhaustive(bitcode, change_set)
    else:
        print("Error: Invalid algorithm!")
        sys.exit(1)
    config_utils.print_config(new_conf, "optimized_config.json")
    
    end_time = time.perf_counter()
    print(f">>> Perci-Tuner(Py): Search time: {(end_time - start_time):.2e} s")
    print(">>> Perci-Tuner(Py): Search finished! Total run times:", run_times, "Fail times:", transform.RUNNING_FAIL_TIMES, "Change set len:", changes_set_len)
    print(">>> Perci-Tuner(Py): Config: " + "optimized_config.json", "IR:", OPT_PREFIX + bitcode_name, "Optimized_time:", min_time, "Original_time:", original_time, "Acc rate:", 100.0 * (original_time - min_time) / min_time, "%")

    baseline_test(bitcode, original_conf, original_time, original_result)


if __name__ == "__main__":
    main()