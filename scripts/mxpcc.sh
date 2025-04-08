#!/bin/bash

# @Author: Guocheng —— NUDT

################################################################################
# Run PerciTuner automatically
# sh mxpcc.sh src_files -mode=config/search/change/multi-version
# config mode: generate config.json + IR + exec for src_files
# search mode: config mode + search for optimized config.json + IR + exec
# change mode: config mode + change src_files precisions according to config.json input
# multi-version mode: merge original and optimized executables
################################################################################

cd $PERCI_TUNER

MODE_FLAG=""

# config-generation option
MXP_FLAG=false  # check pragma precision & autofp vars if true, or search all vars

# search/multi-version mode option, default value as below
RUN_ALGORITHM=dd
THRESHOLD=1e-6
REPEAT_TIMES=1
HISTORY_FLAG=false

# change mode option
TARGET_CONFIG=changed_config.json

# multi-version mode option
MULTI_VERSION_IR=""
MULTI_VERSION_FUNC=""
ORIGINAL_VERSION_FLAG=false

BCS=""
HALF_PRECISION_SUPPORT="-Xclang -fnative-half-type"

for arg in "$@"
do
    # 只处理.c/.cpp/.h/.hpp后缀的文件
    if test -f "$arg" && (test "${arg##*.}" = "c" || test "${arg##*.}" = "cpp" || test "${arg##*.}" = "h" || test "${arg##*.}" = "hpp"); then
        # Only keep path: path/to/a.cpp -> path/to/
        SRC_PATH=$(echo "$arg" | sed 's|/[^/]*$||')"/"
        # Trim prefix & suffix: path/to/a.cpp-> a
        SRC_NAME=$(echo "$arg" | sed 's|.*/||' | sed 's|\.[^.]*$||')
        # Trim suffix: path/to/a.cpp-> path/to/a
        SRC_NO_SUFFIX=$SRC_PATH$SRC_NAME

        echo "SRC_PATH: $SRC_PATH"
        echo "SRC_NAME: $SRC_NAME"
        echo "SRC_NO_SUFFIX: $SRC_NO_SUFFIX"

        # -fno-discard-value-names is needed for [#pragma precision] support
        clang -g -emit-llvm -c $arg $HALF_PRECISION_SUPPORT -fno-discard-value-names -o $SRC_NO_SUFFIX.bc
        BCS="$BCS $SRC_NO_SUFFIX.bc"  # append bc name to BCS
    elif test "$arg" = "-mode=config"; then
        MODE_FLAG="config"
    elif test "$arg" = "-mode=search"; then
        MODE_FLAG="search"
        echo "search"
    elif test "$arg" = "-mode=change"; then
        MODE_FLAG="change"
    elif test "$arg" = "-mode=multi-version"; then
        MODE_FLAG="multi-version"
    elif test "$arg" = "-exhaust"; then
        RUN_ALGORITHM=exhaust
    elif test "$arg" = "-history"; then
        HISTORY_FLAG=true
    elif test "$arg" = "-mxp"; then
        MXP_FLAG=true
    elif test "$arg" = "-original-version"; then
        ORIGINAL_VERSION_FLAG=true
    elif test "${arg#-target=}" != "$arg"; then
        TARGET_CONFIG="${arg#-target=}"
    elif test "${arg#-multi-ir=}" != "$arg"; then
        MULTI_VERSION_IR="${arg#-multi-ir=}"
    elif test "${arg#-multi-func=}" != "$arg"; then
        MULTI_VERSION_FUNC="${arg#-multi-func=}"
    elif test "${arg#-thres=}" != "$arg"; then
        THRESHOLD="${arg#-thres=}"
    elif test "${arg#-repeat=}" != "$arg"; then
        REPEAT_TIMES="${arg#-repeat=}"
    else
        echo "Error: -mode parameter is required"
        echo "  -mode=config        Generate config file (config.json) and executable"
        echo "  -mode=search        Search for optimized config (includes config mode)"
        echo "  -mode=change        Modify source code precision according to config"
        echo "  -mode=multi-version Generate multi-version executable (includes search mode)"
        echo ""
        echo "Parameters for different mode:"
        echo "  -exhaust           (search | multi-version) Use exhaustive search algorithm"
        echo "  -thres=<1e-6>      (search | multi-version) Set error threshold (default 1e-6)"
        echo "  -repeat=<1>        (search | multi-version) Set repeat count (default 1)"
        echo "  -history           (search | multi-version) Keep search history (default false)"
        echo "  -mxp               (config | search | multi-version) Config only includes pragma precision and autofp variables"
        echo "  -original-version  (multi-version) Use original version in multi-version mode (default false)"
        echo "  -target            (change) Set target config file (default changed_config.json)"
        echo "  -multi-ir          (multi-version) Set another version IR"
        echo "  -multi-func        (multi-version) Set multi-version function name"
        exit 1
    fi
done

llvm-link $BCS -o output.bc

# 生成源程序的config，还能移除pragma和__autofp防止后端不识别报错
opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='search_function_call,search_variable_decl' --mxp=$MXP_FLAG output.bc -o output.bc
clang -O2 -o output output.bc -lgsl -lblas -lm
echo "[Original] Config: config.json, IR: output.bc, Executable: output"

if test $MODE_FLAG = "change"; then
    # 根据config修改源程序
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='change_function_call,change_variable_decl' --variable-config=$TARGET_CONFIG --function-config=$TARGET_CONFIG  output.bc -o changed_output.bc
    clang -O2 -o changed_output changed_output.bc -lgsl -lblas -lm
    echo "[Changed] IR: changed_output.bc, Executable: changed_output"
fi

if test $MODE_FLAG = "search"; then
    # 动态搜索降精配置
    python3 scripts/search.py -algorithm=$RUN_ALGORITHM -thres=$THRESHOLD -repeat=$REPEAT_TIMES -history=$HISTORY_FLAG
    clang -O2 -o optimized_output optimized_output.bc -lgsl -lblas -lm
    echo "[Optimized] Config: optimized_output.bc.json, IR: optimized_output.bc, Executable: optimized_output"
fi

if test $MODE_FLAG = "multi-version"; then
    # 合并原始程序和优化后的程序
    if test -z "$MULTI_VERSION_IR" -o -z "$MULTI_VERSION_FUNC"; then
        echo $MULTI_VERSION_IR
        echo $MULTI_VERSION_FUNC
        echo "Error: multi-version mode requires both -multi-ir and -multi-func parameters"
        echo "Usage: -multi-ir=<another_ir_file> -multi-func=<function_name>"
        exit 1
    fi
    # TODO, export is useless, should be run by user in shell.
    export PERCI_TUNER_ORIGINAL_VERSION=$ORIGINAL_VERSION_FLAG 
    clang -S -emit-llvm -fno-discard-value-names -o mvselect.bc lib/mvselect.c
    llvm-link output.bc mvselect.bc -o output+mvselect.bc
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='merge_multi' --ir-multi=$MULTI_VERSION_IR --function-multi=$MULTI_VERSION_FUNC output+mvselect.bc -o merged.bc
    clang -O2 -o merged merged.bc -lgsl -lblas -lm
    echo "[Merged] IR: merged.bc, Executable: merged"
fi
