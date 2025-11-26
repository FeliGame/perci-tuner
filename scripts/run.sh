#!/bin/bash

# @Author: Guocheng —— NUDT

####################################
# Build PerciTuner automatically   #
####################################

cd $PERCI_TUNER
# rm -rf build
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake ..
make
cd $PERCI_TUNER

################################################################################
# Run PerciTuner automatically
################################################################################

MODE_FLAG=""
SOURCE_FILES=""

MXP_FLAG=false  # check pragma precision & autofp vars if true, or search all vars
FPE_FLAG=false
CHECK_RESULT_FLAG=false  # 是否插桩相对误差输出（适用double-double，更精准）

# search/multi-version mode option, default value as below
RUN_ALGORITHM=dd
THRESHOLD=1e-6
REPEAT_TIMES=1
TIMEOUT=60.0
HISTORY_FLAG=true
SHUFFLED_FLAG=false
THREADS=1

# convert fp128 to double-double
DD_FLAG=false

# change mode option
TARGET_CONFIG=changed_config.json

# multi-version mode option
MULTI_VERSION_IR=""
MULTI_VERSION_FUNC=""
ORIGINAL_VERSION_FLAG=false

BCS=""
HALF_PRECISION_SUPPORT="-Xclang -fnative-half-type"

LINK_LIBS=""
LINK_DIRS=""
RUNTIME_LIB_DIRS=""
INCLUDE_DIRS=""
CFLAGS="" # -fno-builtin 防止不同优化等级导致的结果差异

# 遍历目录获取 .c 和 .cpp 文件
get_source_files_from_dir() {
    local dir="$1"
    if [ -d "$dir" ]; then
        find "$dir" -type f \( -name "*.c" -o -name "*.cpp" \) -print0 | xargs -0 printf "%s " >> /tmp/source_files.txt
        SOURCE_FILES="$SOURCE_FILES $(cat /tmp/source_files.txt)"
        rm /tmp/source_files.txt
    fi
}

is_x86_64() {
    [ "$(uname -m)" = "x86_64" ]
}

if is_x86_64; then
    LINK_LIBS="$LINK_LIBS -lquadmath"
fi

for arg in "$@"
do
    # 检查参数是否包含斜杠，若包含则视为目录
    if test "$arg" != "${arg#*/}"; then
        get_source_files_from_dir "$arg"
    fi
    # 只处理.c/.cpp/.h/.hpp后缀的文件
    if test -f "$arg" & (test "${arg##*.}" = "c" || test "${arg##*.}" = "cpp" || test "${arg##*.}" = "h" || test "${arg##*.}" = "hpp"); then
        SOURCE_FILES="$SOURCE_FILES $arg"
    # 识别以 -l 开头的参数并添加到 LINK_LIBS 中
    elif test "${arg#-l}" != "$arg"; then
        LINK_LIBS="$LINK_LIBS $arg"
    elif test "${arg#-L}" != "$arg"; then
        LINK_DIRS="$LINK_DIRS $arg"
    elif test "${arg#-I}" != "$arg"; then
        INCLUDE_DIRS="$INCLUDE_DIRS $arg"
    elif test "${arg#-O}" != "$arg"; then
        CFLAGS="$arg"
    elif test "${arg#-j}" != "$arg"; then
        # 提取-j后的数字部分
        THREADS="${arg#-j}"
        # 验证是否为正整数（非空且为大于0的数字）
        if test -z "$THREADS" || ! test "$THREADS" -gt 0 2>/dev/null; then
            echo "Error: -j requires a positive integer argument, got '$THREADS'" >&2
            exit 1
        fi
    elif test "$arg" = "generate-config" -o "$arg" = "search-optimize" -o "$arg" = "change-by-config" -o "$arg" = "multi-version"; then
        MODE_FLAG="$arg"
    elif test "${arg#-algorithm=}" != "$arg"; then
        RUN_ALGORITHM="${arg#-algorithm=}"
    elif test "$arg" = "-shuffled"; then
        SHUFFLED_FLAG=true
    elif test "$arg" = "-history"; then
        HISTORY_FLAG=true
    elif test "$arg" = "-check-result"; then
        CHECK_RESULT_FLAG=true
        LINK_DIRS="$LINK_DIRS -L$PERCI_TUNER/lib/check_result"
        LINK_LIBS="$LINK_LIBS -lcheck_result"
        RUNTIME_LIB_DIRS="-Wl,-rpath=$PERCI_TUNER/lib/check_result"
    elif test "$arg" = "-mxp"; then
        MXP_FLAG=true
        CHECK_RESULT_FLAG=true
        LINK_DIRS="$LINK_DIRS -L$PERCI_TUNER/lib/check_result"
        LINK_LIBS="$LINK_LIBS -lcheck_result"
        RUNTIME_LIB_DIRS="-Wl,-rpath=$PERCI_TUNER/lib/check_result"
    elif test "$arg" = "-fp128todd"; then
        DD_FLAG=true
    elif test "$arg" = "-fpe"; then
        FPE_FLAG=true
        MXP_FLAG=true
        CHECK_RESULT_FLAG=true
        LINK_DIRS="$LINK_DIRS -L$PERCI_TUNER/lib/check_result"
        LINK_LIBS="$LINK_LIBS -lcheck_result"
        RUNTIME_LIB_DIRS="-Wl,-rpath=$PERCI_TUNER/lib/check_result"
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
        echo "Usage: run.sh [options] src_files -l<library> -L<lib_path> -I<include_path> -O<optimize level, such as -O2, WARNING: may cause unexpected result!>"
        echo "Without modes option below, run.sh calls clang directly"
        echo "Available modes:"
        echo "  generate-config   Generate config file (config.json) and executable"
        echo "  search-optimize   Search for optimized config (run generate-config first)"
        echo "  change-by-config  Modify source code precision according to target config"
        echo "  multi-version     Generate multi-version executable"
        echo ""
        echo "Options for different mode:"
        echo "  -algorithm=exhaust/dd/hidd  (search-optimize) Select search algorithm (default dd)"
        echo "  -thres=<1e-6>               (search-optimize) Set error threshold (default 1e-6)"
        echo "  -repeat=<1>                 (search-optimize) Set repeat count (default 1)"
        echo "  -timeout=<60.0>             (search-optimize) Set run-time timeout seconds(default 60.0sec)"
        echo "  -shuffled                   (search-optimize) Shuffle searched variables (default false)"
        echo "  -j<threads>                 (search-optimize) Set threads count for dd-algorithm(default 1)"
        echo "  -history                    (search-optimize) Keep search history (default false)"
        echo "  -check-result               (all modes) Print relative error in exec"
        echo "  -fp128todd                  (all modes) fp128 regarded as double-double. On x86_64, must use printf("%Qa", result)!"
        echo "  -mxp                        (all modes) Config only includes pragma precision and autofp variables, will auto enable -check-result"
        echo "  -fpe                        (all modes) Print Floating-Point Exception, will auto enable -mxp & -check-result"
        echo "  -target                     (change-by-config) Set target config file (default changed_config.json)"
        echo "  -original-version           (multi-version) Use original version in multi-version mode (default false)"
        echo "  -multi-ir                   (multi-version) Set another version IR"
        echo "  -multi-func                 (multi-version) Set multi-version function name"
        echo ""
        echo "Examples:"
        echo "  run.sh generate-config -mxp src.c -lm"
        echo "  run.sh search-optimize -mxp -algorithm=exhaust -thres=1e-5 -repeat=10 src.c -lm -lgsl -lblas"
        echo "  run.sh change-by-config -target=config.json src.c"
        echo "  run.sh multi-version -mxp -original-version -multi-ir=optimized.bc -multi-func=main src.c"
        echo ""
        echo "__autofp Usage (available while -mxp on):"
        echo "  An flexible floating-type for var declaration, var's actual precision is up to optimized result (driven by search-optimize mode)"
        echo "  Precision range: [fp16, float, double, long double]"
        echo "  Code example: __autofp var1, var2[100];"
        echo ""
        echo "#pragma Usage (available while -mxp on):"
        echo "  1. #pragma precision region: Only vars & func calls in {} region will be optimized"
        echo "  2. #pragma precision error result_var(1e-6): Specify relative-error threshold 1e-6 for result_var while search optimized config (driven by search-optimize mode)"
        echo "      must be inserted in result_var's decl scope"
        echo "  3. #pragma precision range(precisions0), var1(precisions1), var2(precisions2)...: Specify precision range for vars in region, precisions0 is default precision range, precisons1 is precision range for var1..."
        echo "      must be inserted inside #pragma precision region"
        echo "      Available Precision values: [__float128, double, float, __fp16], __float128 is long double in C/C++"
        echo "  Code example:  
                void fun()
                {
                    long double fun_result = 0.0;
                    double t1, t2, s1;
                    #pragma precision error fun_result(2.1e-7)
                    #pragma precision region
                    {
                        #pragma precision range(__float128, double, float, __fp16), t2(double, float)
                        t2 += 1.0;
                        s1 = s1 + sqrt (h*h + (t2 - t1)*(t2 - t1));
                        t1 = t2; 
                    }
                    fun_result = t1;
                }
            "
        exit 0
    fi
done
# 如果不带有 -mode 参数，直接调用 clang
if test -z "$MODE_FLAG"; then
    echo "call clang..."
    clang "$@"
    exit $?
fi

clang --version

# 统一处理源文件
for src_file in $SOURCE_FILES; do
    # 检查文件是否存在
    if ! test -f "$src_file"; then
        echo "Error: File not found: $src_file"
        exit 1
    fi
    if is_x86_64; then
        FP128_SUPPORT="-mlong-double-128"
        echo "Enable float128 support for x86_64"
    fi
    if $FPE_FLAG; then
        # FPE模式预处理
        clang -S -emit-llvm -g -gdwarf-2 -funwind-tables -mllvm -mxpfpe "$src_file" $FP128_SUPPORT $INCLUDE_DIRS $HALF_PRECISION_SUPPORT -fno-discard-value-names -o "$src_file.bc"
    else
        # 其他模式正常处理
        clang -g -emit-llvm -c "$src_file" $FP128_SUPPORT $INCLUDE_DIRS $HALF_PRECISION_SUPPORT -fno-discard-value-names -o "$src_file.bc"
    fi
    
    BCS="$BCS $src_file.bc"
done

llvm-link $BCS -o output.bc

if test $MODE_FLAG = "change-by-config"; then
    # 根据config修改源程序
    PASSES="change_function_call,change_variable_decl"
    if $DD_FLAG; then
        PASSES="$PASSES,fp128_to_dd"
        if is_x86_64; then
            DD_SUPPORT="$PERCI_TUNER/lib/dd_real/libautop-x86.a"
        else
            DD_SUPPORT="$PERCI_TUNER/lib/dd_real/libautop.a"
        fi
    fi
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes=$PASSES --variable-config=$TARGET_CONFIG --function-config=$TARGET_CONFIG  output.bc -o changed_output.bc
    clang $CFLAGS -o changed_output changed_output.bc $LINK_LIBS $LINK_DIRS $DD_SUPPORT
    echo "[Changed] IR: changed_output.bc, Executable: changed_output"
else
    # 生成源程序的config，还能移除pragma和__autofp防止后端不识别报错
    if $CHECK_RESULT_FLAG; then
        # 第一次获取原始值
        opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='record_result,remove_pragma' --is-original=true output.bc -o mxp.bc
        clang $CFLAGS -o output mxp.bc $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS
        rm mxp.bc
        # 捕获最后一行输出并设置环境变量
        LAST_LINE=$(./output | tail -n 1)
        export PERCI_TUNER_ORIGINAL_RESULT="$LAST_LINE"
        echo "Set PERCI_TUNER_ORIGINAL_RESULT=$PERCI_TUNER_ORIGINAL_RESULT"
    fi
    PASSES="print_config,remove_pragma"
    if [ "$DD_FLAG" = "true" ] && [ "$MODE_FLAG" = "generate-config" ]; then
        PASSES="$PASSES,fp128_to_dd"
        if is_x86_64; then
            DD_SUPPORT="$PERCI_TUNER/lib/dd_real/libautop-x86.a"
        else
            DD_SUPPORT="$PERCI_TUNER/lib/dd_real/libautop.a"
        fi
    fi
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes=$PASSES --mxp=$MXP_FLAG output.bc -o output.bc
    clang $CFLAGS -o output output.bc $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS $DD_SUPPORT
    echo "[Original] Config: config.json, IR: output.bc, Executable: output"
fi

if test $MODE_FLAG = "search-optimize"; then
    # 动态搜索降精配置
    python3 $PERCI_TUNER/scripts/search.py -algorithm=$RUN_ALGORITHM -thres=$THRESHOLD -repeat=$REPEAT_TIMES -timeout=$TIMEOUT -history=$HISTORY_FLAG -shuffled=$SHUFFLED_FLAG -threads=$THREADS -mxp=$MXP_FLAG -fp128todd=$DD_FLAG output.bc $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS $CFLAGS
    echo "[Optimized] Config: optimized_config.json, IR: optimized_output.bc, Executable: optimized_output"
fi

if test $MODE_FLAG = "multi-version"; then
    # 合并原始程序和优化后的程序
    if test -z "$MULTI_VERSION_IR" -o -z "$MULTI_VERSION_FUNC"; then
        echo $MULTI_VERSION_IR
        echo $MULTI_VERSION_FUNC
        echo "Error: multi-version mode requires both -multi-ir and -multi-func options"
        echo "Usage: -multi-ir=<another_ir_file> -multi-func=<function_name>"
        exit 1
    fi
    export PERCI_TUNER_ORIGINAL_VERSION=$ORIGINAL_VERSION_FLAG 
    clang -S -emit-llvm -fno-discard-value-names -o mvselect.bc $PERCI_TUNER/lib/mvselect.c
    llvm-link output.bc mvselect.bc -o output+mvselect.bc
    opt -load-pass-plugin $PERCI_TUNER/build/PerciTuner.so -S -passes='merge_multi' --ir-multi=$MULTI_VERSION_IR --function-multi=$MULTI_VERSION_FUNC output+mvselect.bc -o merged.bc
    clang $CFLAGS -o merged merged.bc $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS
    echo "[Merged] IR: merged.bc, Executable: merged"
fi

if $FPE_FLAG; then
    # 处理fp异常（需要mxp版本LLVM）- 生成可执行文件
    clang $CFLAGS -o fpe_opt -rdynamic -funwind-tables optimized_output.bc -lm $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS
fi
