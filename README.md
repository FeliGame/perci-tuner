# Perci-Tuner

Perci-Tuner increases program's runtime speed while keeping errors within an acceptable range, by reducing floating-point precision.
Perci-Tuner uses the New PassManager to be compatible with LLVM17, and features a modular design for easier expansion and modification.

# User Manual

First things first, install LLVM (recommended version: 17.0.1).

## Add Environment Variable

Add this into ~/.bashrc:

`export PERCI_TUNER="/path/to/perci-tuner/"`

Then run `source ~/.bashrc` to enable it:

## Building

`sh scripts/build.sh`

This will generate a pass.so under build/ directory.  

## Run Perci-Tuner

`sh scripts/run.sh /path/to/src.cpp "Optional Flags"`

This will generate results (IR and related files) under the src folder.

Optional Flags should be in such format: 

`sh scripts/run.sh /path/to/src.cpp "--search-entry=main"`

It means Perci-Tuner should search from main function (Implemented in CreateCallDependency).

# Structure

* /lib: Source code of IR passes.
* /scripts:  Run-time score logging with Python & Shell.
* /benchmarks: Some code to test Perci-Tuner.