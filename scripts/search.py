#!/usr/bin/env python3

# @Author: LinLanfeng, Guocheng —— NUDT

import sys, json, os, time, copy, math
import config_utils, data_utils, transform
import exhaust_algorithm, dd_algorithm, hidd_algorithm, pedd_algorithm, algorithm_utils

def parse_arguments():
    params = {
        '-compiler': 'clang',
        '-ir': 'output.bc',
        '-config': 'config.json',
        '-thres': 1e-6,
        '-repeat': 1,
        '-history': True,
        '-algorithm': 'dd',
        '-mxp': False,
        '-timeout': 1.5,
        '-time-mode': 'program',
        '-shuffled': False,
        '-partition': 'sorted_partition.json',
        '-fp128todd': False,
        '-mix-binop': True,
        '-search-range': '128,64,32,16',
        '-link-libs': [],
        '-link-dirs': [],
        '-rpath': [],
        '-cflags': [],
	    '-run-args': [],
        '-base-time': '',
        '-base-result': '',
    }
    
    for arg in sys.argv[1:]:
        if arg.startswith('-l'):
            params['-link-libs'].append(arg)
        elif arg.startswith('-L'):
            params['-link-dirs'].append(arg)
        elif arg.startswith('-Wl,-rpath'):
            params['-rpath'].append(arg)
        elif arg.startswith('-arg'):
            k, v = arg.split('=', 1)
            if v != '':
                params['-run-args'].append(v)
        elif '=' in arg:
            key, value = arg.split('=', 1)
            if key in params:
                if key == '-compiler':
                    params[key] = value
                elif key == '-thres':
                    params[key] = float(value)
                elif key == '-repeat':
                    params[key] = int(value)
                elif key == '-history':
                    params[key] = value.lower() in ('true', '1', 'yes')
                elif key == '-timeout':
                    params[key] = float(value)
                elif key == '-shuffled':
                    params[key] = value.lower() in ('true', '1', 'yes')
                elif key == '-mxp':
                    params[key] = value.lower() in ('true', '1', 'yes')
                elif key == '-fp128todd':
                    params[key] = value.lower() in ('true', '1', 'yes')
                elif key == '-mix-binop':
                    params[key] = value
                elif key == '-base-time' and value != '':
                    params[key] = float(value)
                elif key == '-base-result' and value != '':
                    params[key] = value
                else:
                    params[key] = value
        elif arg.startswith('-'):
            params['-cflags'].append(arg)
        
    return params

def baseline_test(bitcode, original_conf):
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
                        if var['type'].endswith('*'):
                            var['type'] = prec + '*'
                        elif '[' in var['type']:
                            arr_size = var['type'].split('[')[1].split(']')[0]
                            var['type'] = f"{prec}[{arr_size}]"
                        else:
                            var['type'] = prec
        
        # 修改结构体成员精度
        if bench_conf['structs']:
            for struct in bench_conf.get('structs', {}):
                if bench_conf['structs'][struct]:
                    for member in bench_conf['structs'][struct]:
                        if any(p in member['type'] for p in config_utils.PRECISIONS):
                            if member['type'].endswith('*'):
                                member['type'] = prec + '*'
                            elif '[' in member['type']:
                                arr_size = member['type'].split('[')[1].split(']')[0]
                                member['type'] = f"{prec}[{arr_size}]"  
                            else:
                                member['type'] = prec
        
        # 修改函数调用精度
        if bench_conf['calls']:
            for caller in bench_conf.get('calls', {}):
                if bench_conf['calls'][caller]:
                    for call in bench_conf['calls'][caller]:
                        func_name = call['type']
                        if func_name in config_utils.FUNCTION_PRECISIONS:
                            # 根据精度类型选择对应的函数后缀
                            if prec == "long double":
                                call['type'] = func_name[:-1] + 'l' if func_name[-1] in ('f', 'l') else func_name + 'l'
                            elif prec == "double":
                                call['type'] = func_name[:-1] if func_name[-1] in ('f', 'l') else func_name
                            elif prec == "float":
                                call['type'] = func_name[:-1] + 'f' if func_name[-1] in ('f', 'l') else func_name + 'f'
                            elif prec == "__fp16":
                                call['type'] = func_name[:-1] + 'f' if func_name[-1] in ('f', 'l') else func_name + 'f'
        
        # 生成.json
        config_utils.print_config(bench_conf, filename)
        
        # 运行测试配置
        algorithm_utils.run_config(bitcode, bench_conf, [], skip_runned=False)
        bench_time = transform.current_run_time
        bench_result = transform.current_run_result
        
        # 计算结果指标
        speedup = (data_utils.ORIGINAL_TIME - bench_time) / bench_time * 100
        error = data_utils.get_rel_err(bench_result, data_utils.ORIGINAL_RESULT)
        print(f"{prec:11} | Time: {bench_time:.2e}cycles | "
              f"Speedup: {speedup:6.2f}% | Relative Error: {error:.2e} | Config: {filename}")


def main():
    args = parse_arguments()

    if args['-time-mode'] not in ('program', 'kernel'):
        print("Error: -time-mode must be 'program' or 'kernel'")
        sys.exit(1)

    transform.COMPILER = args['-compiler']
    bitcode = args['-ir']
    original_conf_file = args['-config']
    config_utils.thres = args['-thres']
    try:
        config_utils.set_search_range(args['-search-range'])
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)
    transform.RUNNING_TIMES = args['-repeat']
    transform.TIME_MODE = args['-time-mode']
    transform.show_history = args['-history']
    algorithm = args['-algorithm']
    shuffled = args['-shuffled']
    transform.FP128_TO_DD = args['-fp128todd']
    transform.MIX_BINOP = args['-mix-binop']
    transform.LINK_LIBS = args['-link-libs']
    transform.LINK_DIRS = args['-link-dirs']
    transform.RUNTIME_LIB_DIRS = args['-rpath']
    transform.CFLAGS = args['-cflags']
    transform.RUN_ARGS = args['-run-args']
    algorithm_utils.is_mxp = args['-mxp']
    
    if transform.MIX_BINOP and not transform.FP128_TO_DD:
        print("Warning: mix-binop enabled without fp128todd, mix-binop will be disabled.")
        transform.MIX_BINOP = False

    start_time = time.perf_counter()
    data_utils.delete_data()

    # load the original config from file
    algorithm_utils.original_conf = json.loads(open(original_conf_file, 'r').read())
    config_utils.CURRENT_CONFIG = algorithm_utils.original_conf

    # change_set keep those need to change precision and available precisions
    change_set = config_utils.parse_config(algorithm_utils.original_conf, shuffled)

    print(">>> Perci-Tuner search start at " + time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()))
    print(f"  Compiler: {args['-compiler']}")
    print(f"  IR file: {args['-ir']}")
    print(f"  Config file: {args['-config']}")
    print(f"  Threshold: {config_utils.thres}")
    print(f"  Repeat times: {args['-repeat']}")
    print(f"  Show history: {args['-history']}")
    print(f"  Algorithm: {args['-algorithm']}")
    print(f"  Timeout multiplier: {args['-timeout']}")
    print(f"  Time mode: {args['-time-mode']}")
    print(f"  Shuffled: {args['-shuffled']}")
    print(f"  Search range: {args['-search-range']}")
    print(f"  FP128 to DD: {args['-fp128todd']}")
    print(f"  Double-Double & double mix BinOp: {args['-mix-binop']}")
    print(f"  Link libs: {args['-link-libs']}")
    print(f"  Link dirs: {args['-link-dirs']}")
    print(f"  Runtime lib dirs: {args['-rpath']}")
    print(f"  CFlags: {args['-cflags']}")
    print(f"  Run args: {args['-run-args']}")
    print(f"  MXP: {args['-mxp']}")
    print(f"  Baseline time: {args['-base-time'] if args['-base-time'] else 'auto-detect'}")
    print(f"  Baseline result: {args['-base-result'] if args['-base-result'] else 'auto-detect'}")

    # get the original time and original result for compare
    # change_to_highest_precision(change_set)
    algorithm_utils.run_config(bitcode, algorithm_utils.original_conf, change_set)
    if args['-base-time'] and args['-base-result']:
        data_utils.ORIGINAL_TIME = args['-base-time']
        data_utils.ORIGINAL_RESULT = args['-base-result']
        print(">>> Perci-Tuner: Using manual baseline values")
    else:
        data_utils.ORIGINAL_TIME = transform.current_run_time
        data_utils.ORIGINAL_RESULT = transform.current_run_result
    if data_utils.is_non_finite(data_utils.ORIGINAL_TIME) or data_utils.is_non_finite(data_utils.ORIGINAL_RESULT):
        print(">>> Perci-Tuner Error: baseline produced non-finite time/result, search aborted.")
        sys.exit(1)
    print("original compile time:", transform.current_compile_time, "original run time:", data_utils.ORIGINAL_TIME, "original result:", data_utils.ORIGINAL_RESULT)
    timeout_baseline = (
        transform.current_run_wall_time
        if args['-time-mode'] == 'kernel'
        else data_utils.ORIGINAL_TIME
    )
    transform.COMPILE_TIMEOUT_THRESHOLD = math.ceil(args['-timeout'] * transform.current_compile_time)
    print(">>> Perci-Tuner: Compile timeout threshold set to", transform.COMPILE_TIMEOUT_THRESHOLD, "seconds")
    transform.TIMEOUT_THRESHOLD = math.ceil(args['-timeout'] * timeout_baseline)
    print(">>> Perci-Tuner: Timeout threshold set to", transform.TIMEOUT_THRESHOLD, "seconds")
        
    # start searching
    algorithm_utils.min_time = data_utils.ORIGINAL_TIME
    algorithm_utils.bitcode_name = os.path.basename(bitcode)
    if algorithm.lower() == 'dd':
        dd_algorithm.run(bitcode, change_set)
    elif algorithm.lower() == 'pedd':
        pedd_algorithm.run(bitcode, change_set)
    elif algorithm.lower() == 'exhaust':
        exhaust_algorithm.run(bitcode, change_set)
    elif algorithm.lower() == 'hidd':
        partition_conf_file = args['-partition']
        partition_tree = json.loads(open(partition_conf_file, 'r').read())
        hidd_algorithm.run(bitcode, change_set, partition_tree)
    else:
        print("Error: Invalid algorithm!")
        sys.exit(1)
    
    end_time = time.perf_counter()
    print(f">>> Perci-Tuner Report <<<\nSearch history recorded in run_history.csv\nSearched {algorithm_utils.run_times} configs including {transform.RUNNING_FAIL_TIMES} failed.\nSearch spent: {(end_time - start_time):.2e}s\nSearch space size: {len(change_set)}")
    print("Optimized config index [", algorithm_utils.best_config_index, "] saved in config/ IR/ exec/\nOptimized cost:", algorithm_utils.min_time, "s\nOriginal time:", data_utils.ORIGINAL_TIME, "s\nAcc rate:", 100.0 * (data_utils.ORIGINAL_TIME - algorithm_utils.min_time) / algorithm_utils.min_time, "%")

    # baseline_test(bitcode, algorithm_utils.original_conf)


if __name__ == "__main__":
    main()
