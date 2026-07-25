#!/usr/bin/env python3

import config_utils, transform, algorithm_utils

def exhaustive_search(bitcode, change_set):
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
        
        new_config = config_utils.rebuild(temp_change_set)
        
        if algorithm_utils.run_config(bitcode, new_config, temp_change_set) and transform.current_run_time < algorithm_utils.min_time:
            algorithm_utils.update_opt(transform.current_run_time)
            candidates.append((new_config, algorithm_utils.min_time))

    return candidates

def run(bitcode, change_set):
    candidates = exhaustive_search(bitcode, change_set)
    return candidates[0][0] if candidates else algorithm_utils.original_conf

