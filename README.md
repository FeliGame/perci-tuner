# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point variables & C-Math function calls' precision.
Perci-Tuner uses the New PassManager to be compatible with LLVM17, and features a modular design for easier expansion and modification.

# User Manual

First things first, install LLVM (recommended version: 17.0.6) build `-DCMAKE_BUILD_TYPE=Release` option, or Perci-Tuner might crash.

```sh
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git checkout -b llvm-17 llvmorg-17.0.6
```

If you want to use -mxp option, install mxp version of LLVM, then build LLVM. https://mlir.llvm.org/getting_started/
``` sh
cmake -G Ninja \
  -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD="host" \
  -DLLVM_ENABLE_PROJECTS="clang" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLLVM_BUILD_UTILS=ON \
  -DLLVM_BUILD_TOOLS=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DLLVM_ENABLE_ASSERTIONS=OFF

cmake --build build -j8
cmake --install build --prefix /usr/local
```

## Add Environment Variable

Add this into ~/.bashrc:

`export PERCI_TUNER="/path/to/perci-tuner/"`

Then run `source ~/.bashrc` to enable it:

## Build & Run Perci-Tuner

`sh scripts/run.sh <generate-config|change-by-config|search-optimize> src_files [Optional Flags]`

Check run.sh for more help info.

# Limitations

* Perci-Tuner enable mix precision for floating-type function return, such as 'long double foo(...) -> double foo(...)', but not support floating-type pointer function return, such as 'long double* foo(...) -> double* foo(...)'
* Perci-Tuner performance not optimal under mix-precision fmuladd case. In clang, fmuladd with diff precision ops should split into fmul + fadd, and merge into fmuladd when operands are same. While Perci-Tuner doesn't support flexible split and merge.
* Perci-Tuner won't change malloc size. In this case, we only recommend change config to lower precision to avoid potential memory overflow.
