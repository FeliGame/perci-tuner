#!/bin/bash

# @Author: Guocheng —— NUDT

################################################################################
# Run PerciTuner automatically
################################################################################

MODE_FLAG=""
SOURCE_FILES=""

# config-generation option
MXP_FLAG=false  # check pragma precision & autofp vars if true, or search all vars

# search/multi-version mode option, default value as below
RUN_ALGORITHM=dd
THRESHOLD=1e-6
REPEAT_TIMES=1
TIMEOUT=5.0
HISTORY_FLAG=false
SHUFFLED_FLAG=false

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
    if test -f "$arg" & (test "${arg##*.}" = "c" || test "${arg##*.}" = "cpp" || test "${arg##*.}" = "h" || test "${arg##*.}" = "hpp"); then
        SOURCE_FILES="$SOURCE_FILES $arg"
    elif test "$arg" = "-mode=generate-config"; then
        MODE_FLAG="generate-config"
    elif test "$arg" = "-mode=search-optimize"; then
        MODE_FLAG="search-optimize"
    elif test "$arg" = "-mode=change-by-config"; then
        MODE_FLAG="change-by-config"
    elif test "$arg" = "-mode=multi-version"; then
        MODE_FLAG="multi-version"
    elif test "$arg" = "-mode=fpe"; then
        MODE_FLAG="fpe"
    elif test "$arg" = "-algorithm=exhaust"; then
        RUN_ALGORITHM=exhaust
    elif test "$arg" = "-algorithm=dd"; then
        RUN_ALGORITHM=dd
    elif test "$arg" = "-algorithm=hidd"; then
        RUN_ALGORITHM=hidd
    elif test "$arg" = "-shuffled"; then
        SHUFFLED_FLAG=true
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
    elif test "${arg#-timeout=}" != "$arg"; then
        TIMEOUT="${arg#-timeout=}"
    elif test "$arg" = "-h" -o "$arg" = "--help"; then 
        echo "Usage: mxpcc.sh [options] src_files"
        echo "Perci-Tuner Options:"
        echo "  -mode=generate-config   Generate config file (config.json) and executable"
        echo "  -mode=search-optimize   Search for optimized config (includes config mode)"
        echo "  -mode=change-by-config  Modify source code precision according to target config"
        echo "  -mode=multi-version     Generate multi-version executable (includes search mode)"
        echo "  -mode=fpe               Output Floating-Point Exception after searching for optimized config"
        echo ""
        echo "Parameters for different mode:"
        echo "  -algorithm=exhaust/dd/hidd  (search-optimize | fpe) Select search algorithm (default dd)"
        echo "  -thres=<1e-6>               (search-optimize | fpe) Set error threshold (default 1e-6)"
        echo "  -repeat=<1>                 (search-optimize | fpe) Set repeat count (default 1)"
        echo "  -timeout=<5.0>              (search-optimize | fpe) Set run-time timeout seconds(default 5.0sec)"
        echo "  -shuffled                   (search-optimize | fpe) Shuffle searched variables (default false)"
        echo "  -history                    (search-optimize | fpe) Keep search history (default false)"
        echo "  -mxp                        (all modes) Config only includes pragma precision and autofp variables"
        echo "  -target                     (change-by-config) Set target config file (default changed_config.json)"
        echo "  -original-version           (multi-version) Use original version in multi-version mode (default false)"
        echo "  -multi-ir                   (multi-version) Set another version IR"
        echo "  -multi-func                 (multi-version) Set multi-version function name"
        echo ""
        echo "Examples:"
        echo "  mxpcc.sh -mode=generate-config -mxp src.c"
        echo "  mxpcc.sh -mode=search-optimize -mxp -algorithm=exhaust -thres=1e-5 -repeat=10 src.c "
        echo "  mxpcc.sh -mode=change-by-config -target=config.json src.c"
        echo "  mxpcc.sh -mode=multi-version -mxp -original-version -multi-ir=optimized.bc -multi-func=main src.c"
        exit 0
    fi
done

if test -z "$MODE_FLAG"; then
    clang "$@"
    exit $?
fi

# 统一处理源文件
for src_file in $SOURCE_FILES; do
    # 检查文件是否存在
    if ! test -f "$src_file"; then
        echo "Error: File not found: $src_file"
        exit 1
    fi
    if test $MODE_FLAG = "fpe"; then
        # FPE模式预处理
        clang -S -emit-llvm -g -gdwarf-2 -funwind-tables -mllvm -mxpfpe "$src_file" -fno-discard-value-names -o "$src_file.bc"
    else
        # 其他模式正常处理
        clang -g -emit-llvm -c "$src_file" $HALF_PRECISION_SUPPORT -fno-discard-value-names -o "$src_file.bc"
    fi
    
    BCS="$BCS $src_file.bc"
done

llvm-link $BCS -o output.bc

if test $MODE_FLAG != "change-by-config"; then
    # 生成源程序的config，还能移除pragma和__autofp防止后端不识别报错
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='print_config,remove_pragma' --mxp=$MXP_FLAG output.bc -o output.bc
    clang -O2 -o output output.bc -lgsl -lblas -lm
    echo "[Original] Config: config.json, IR: output.bc, Executable: output"
fi

if test $MODE_FLAG = "change-by-config"; then
    # 根据config修改源程序
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='change_function_call,change_variable_decl' --variable-config=$TARGET_CONFIG --function-config=$TARGET_CONFIG  output.bc -o changed_output.bc
    clang -O2 -o changed_output changed_output.bc -lgsl -lblas -lm
    echo "[Changed] IR: changed_output.bc, Executable: changed_output"
fi

if test $MODE_FLAG = "search-optimize" -o $MODE_FLAG = "fpe"; then
    # 动态搜索降精配置
    python3 $PERCI_TUNER/scripts/search.py -algorithm=$RUN_ALGORITHM -thres=$THRESHOLD -repeat=$REPEAT_TIMES -timeout=$TIMEOUT -history=$HISTORY_FLAG -shuffled=$SHUFFLED_FLAG -mxp=$MXP_FLAG output.bc
    clang -O2 -o optimized_output optimized_output.bc -lgsl -lblas -lm
    echo "[Optimized] Config: optimized_config.json, IR: optimized_output.bc, Executable: optimized_output"
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
    export PERCI_TUNER_ORIGINAL_VERSION=$ORIGINAL_VERSION_FLAG 
    clang -S -emit-llvm -fno-discard-value-names -o mvselect.bc $PERCI_TUNER/lib/mvselect.c
    llvm-link output.bc mvselect.bc -o output+mvselect.bc
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='merge_multi' --ir-multi=$MULTI_VERSION_IR --function-multi=$MULTI_VERSION_FUNC output+mvselect.bc -o merged.bc
    clang -O2 -o merged merged.bc -lgsl -lblas -lm
    echo "[Merged] IR: merged.bc, Executable: merged"
fi

if test $MODE_FLAG = "fpe"; then
    # 处理fp异常（需要mxp版本LLVM）- 生成可执行文件
    clang -O2 -o fpe_opt -rdynamic -funwind-tables optimized_output.bc -lm
fi
