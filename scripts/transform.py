#!/usr/bin/env python3

# @Author: LinLanfeng —— NUDT

import os, time
import data_utils
from subprocess import call
import numpy as np
import config_utils

PER_TUNE_PATH = os.path.join(os.getenv("PERCI_TUNER"), "build/PerciTuner.so")
PASSES = "search_functions,config,change_precision"
TEMP_BC_PATH = "temp_output.bc"
TEMP_CONFIG_FILE = "temp_config.json"

# TODO: replace the true command 
# run the current variable config and get the result
def transform(bitcode_file, var_config):

    # write the config to the temp file 
    config_utils.print_config(var_config, TEMP_CONFIG_FILE)

    # run LLVM passes
    #opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='search_functions,config,change_precision' $FLAGS output.bc -o output.bc
    command = ['opt', '-load-pass-plugin', PER_TUNE_PATH, "-S", "-passes=" + PASSES, "--target-config=" + TEMP_CONFIG_FILE , bitcode_file, "-o", TEMP_BC_PATH]
    retval = call(command, stdin=None, stdout=None, stderr=None)
    # return -1 if running LLVM passes fails
    if retval != 0:
        return -1

    # run modified bitcode
    command = ['lli', TEMP_BC_PATH]
    start_time = time.time()
    retval = call(command, stdin=None, stdout=None, stderr=None)
    end_time = time.time()
    # return -2 if crashed when run modified bitcode
    if retval != 0:
        return -2
    
    # store the running time and result
    # TODO:store the true result
    # TODO:get the true time
    data_utils.store_data(end_time - start_time, 1000)

    return 1
