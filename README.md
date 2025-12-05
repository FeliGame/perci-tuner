# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point variables & C-Math function calls' precision.
Perci-Tuner uses the New PassManager to be compatible with LLVM17, and features a modular design for easier expansion and modification.

# User Manual

To keep result accurate, src code must have a single 
`printf("%Lf", (long double)result);` or `printf("%La", (long double)result);`

First things first, install LLVM (recommended version: 17.0.6) build `-DCMAKE_BUILD_TYPE=Release` option, or Perci-Tuner might crash.

```sh
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git checkout -b llvm-17 llvmorg-17.0.6
```

If you want to use -mxp option, install mxp version of LLVM, then build LLVM with(ref: https://mlir.llvm.org/getting_started/):
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

cmake --build build -j8
cmake --install build --prefix /path/to/install
```

## Add Environment Variable

Add this into ~/.bashrc:

`export PERCI_TUNER="/path/to/perci-tuner/"`

Then run `source ~/.bashrc` to enable it:

## Build & Run Perci-Tuner

`sh scripts/run.sh src_files... <command> [Optional Flags]`

Check run.sh for more information.

## Pass Flags

Each Optional Flag should be enclosed in double quotes, and it must start with '--'. Current available flags are shown below:

### --mxp(bool)
Search all floating-point vars in IR if set to false, or search only __autofp vars or vars in #pragma region.
From Pass: print_config.

### --is-original(bool)
Only enabled when --mxp=true. If set true, print result_var as hex format, or print relative error between result_var and env var: PERCI_TUNER_ORIGINAL_RESULT.
From Pass: record_result.

### --variable-config

Customize target precision input filename.
From Pass: change_variable_decl.

### --function-config

Customize target function calls input filename. From Pass: change_function_call.

### --ir-timing-result

Customize IR timing result's output filename. From Pass: ir_timing.

### --ir-multi

Customize another version input IR filename.
From Pass: merge_multi.

### --function-multi

Decide which function should be merged from another version input IR.
From Pass: merge_multi.
