# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point variables & C-Math function calls' precision.
Perci-Tuner uses the New PassManager to be compatible with LLVM17, and features a modular design for easier expansion and modification.

# User Manual

First things first, install LLVM (recommended version: 17.0.6) with `-DCMAKE_BUILD_TYPE=Release` option, or Perci-Tuner might crash.

## Add Environment Variable

Add this into ~/.bashrc:

`export PERCI_TUNER="/path/to/perci-tuner/"`

Then run `source ~/.bashrc` to enable it:

## Building

`sh scripts/build.sh`

This will generate a pass.so under build/ directory.  

## Run Perci-Tuner

`sh scripts/run.sh /path/to/src1.c /path/to/src2.c ... "Optional Flags"`

This will generate results (IR and related files) under the src folder.

`python3 scripts/search.py output.bc config.json 1e-2 10`

Run this will generate optimized precision config file and runnable .bc file, which based on delta-debugging algorithm from Precimonious. The threshold is used for calculating the relative error of cmd output. Last parameter defines the IR's running time during each search iteration.

`sh scripts/run_multi_version.sh output.bc optimized_output.bc function_name`

(Optional) If you want to merge the orginal IR and the optimized IR into a merged IR, you can run this command. Set environment value to decide which IR to use(implemented in mvselect.c):

`export PERCI_TUNER_SPEED_FIRST=TRUE`

## Optional Flags

Each Optional Flag should be enclosed in double quotes, and it must start with '--'. Current available flags are shown below:

### --excluded-functions=excluded_functions.txt

Customize excluded functions for searching, from Pass:search_function_call, implemented in SearchFunctions.

### --target-config

Customize target precision input filename, from Pass:change_precision, implemented in ChangePrecision.

### --ir-timing-result

Customize IR timing result's output filename, from Pass:ir_timing, implemented in IRTiming.

### --ir-multi

Customize another version input IR filename, from
Pass: merge_multi, implemented in MergeMultiVersion.

### --function-multi

Decide which function should be merged from another version input IR, from
Pass: merge_multi, implemented in MergeMultiVersion.
