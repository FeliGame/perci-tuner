# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point variables & C-Math function calls' precision.
Perci-Tuner uses the New PassManager to be compatible with LLVM17, and features a modular design for easier expansion and modification.

# User Manual

First things first, install LLVM (recommended version: 17.0.6) build `-DCMAKE_BUILD_TYPE=Release` option, or Perci-Tuner might crash.

If you want to use -mxp option, install mxp version of LLVM, then build LLVM with:
``` sh
cmake -G Ninja \
  -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD="host;PowerPC" \
  -DLLVM_ENABLE_PROJECTS="clang;mlir" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLLVM_BUILD_UTILS=ON \
  -DLLVM_BUILD_TOOLS=ON \
  -DLLVM_INSTALL_GTEST=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DLLVM_ENABLE_ASSERTIONS=OFF
```

## Add Environment Variable

Add this into ~/.bashrc:

`export PERCI_TUNER="/path/to/perci-tuner/"`

Then run `source ~/.bashrc` to enable it:

## Building

`sh scripts/build.sh`

This will generate a pass.so under build/ directory.  

## Run Perci-Tuner

`sh scripts/mxpcc src_files... -mode=xxx [Optional Flags]`

Check mxpcc for more information.

## Pass Flags

Each Optional Flag should be enclosed in double quotes, and it must start with '--'. Current available flags are shown below:

### --mxp(bool)
Search all floating-point vars in IR if set to false, or search only __autofp vars or vars in #pragma region. From Pass:print_config.

### --variable-config

Customize target precision input filename, from Pass:change_variable_decl.

### --function-config

Customize target function calls input filename, from Pass:change_function_call.

### --ir-timing-result

Customize IR timing result's output filename, from Pass:ir_timing.

### --ir-multi

Customize another version input IR filename, from
Pass: merge_multi.

### --function-multi

Decide which function should be merged from another version input IR, from
Pass: merge_multi.
