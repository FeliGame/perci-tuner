# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point variables & C-Math function calls' precision.
Perci-Tuner uses the New PassManager to be compatible with LLVM17, and features a modular design for easier expansion and modification.

# User Manual

First things first, install LLVM (recommended version: 17.0.6) build `-DCMAKE_BUILD_TYPE=Release` option, or Perci-Tuner might crash.

```sh
git init
git remote add origin https://github.com/llvm/llvm-project.git
git fetch --depth=1 origin refs/tags/llvmorg-17.0.6:refs/tags/llvmorg-17.0.6
git checkout llvmorg-17.0.6
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

## Test

```bash
cd $PERCI_TUNER
sh scripts/build.sh
cd build
ninja check
```
Or run specific test:
```bash
python3 -m lit -v test/xxx
```