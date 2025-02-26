# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point precision.
Perci-Tuner uses the New PassManager to be compatible with LLVM17, and features a modular design for easier expansion and modification.

# User Manual

First things first, install LLVM (recommended version: 17.0.6).

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

`python3 scripts/dd.py output.bc config.json 1e-2 10`

Run this will generate optimized precision config file and runnable .bc file, which based on delta-debugging algorithm from Precimonious. The threshold is used for calculating the relative error of cmd output. Last parameter defines the IR's running time during each search iteration.

## Optional Flags

Each Optional Flag should be enclosed in double quotes, and it must start with '--'. Current available flags are shown below:

### --search-entry=func_name

Customize running entry function for searching, from Pass:search_functions, implemented in SearchFunctions.

### --excluded-functions=excluded_functions.txt

Customize excluded functions for searching, from Pass:search_functions, implemented in SearchFunctions.

### --config-file

Customize searched result's output filename, from Pass:config, implemented in CreatePrecisionConfig.

### --target-config

Customize target precision input filename, from Pass:change-precision, implemented in ChangePrecision.

### --ir-timing-result

Customize IR timing result's output filename, from Pass:ir-timing, implemented in IRTiming.
