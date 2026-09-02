# Perci-Tuner

Perci-Tuner increases C program's runtime speed while keeping errors within an acceptable range, by reducing floating-point variables & C-Math function calls' precision.

## LLVM 22

```sh
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git checkout llvmorg-22.1.8

cmake -G Ninja \
  -S llvm -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD="host" \
  -DLLVM_ENABLE_PROJECTS="clang;mlir" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLLVM_BUILD_UTILS=ON \
  -DLLVM_BUILD_TOOLS=ON \
  -DBUILD_SHARED_LIBS=ON \
  -DLLVM_ENABLE_ASSERTIONS=OFF

cmake --build build
cmake --install build --prefix ${INSTALLDIR}
```
If you want to use -mxp option, install mxp version of LLVM from gitmodule.

## Run

```sh
export PERCI_TUNER="/path/to/perci-tuner/"
sh scripts/run.sh <print-config|change-by-config|search-optimize|check> src_files [Optional Flags]
```
Check run.sh for more help info.

## Test

```sh
cd $PERCI_TUNER
sh scripts/run.sh check
```
Or run specific test:
```sh
python3 -m lit -v test/xxx
```

## Visualize search

After finishing a search, generate `run_history.png` with:
```bash
python3 scripts/visualize.py
```
The PNG generated from `run_history.csv`: 
* Each pie is a runned config marked with `run_id`
* Each line is a generation of configs, they were born in a common parent.
* `parent` means the run_id that current config search from
* Outer cycle means bitwidth of elements:
```
16-green, 32-blue, 64-yellow, 128-red
```
* Inner cycle means run status: 
```
Black: RUNTIME-ERROR (unexpected)
Red: out-of-errorthreshold
Yellow: Run slower than original
Green: Run faster than original
✅: The fastest config until current
```