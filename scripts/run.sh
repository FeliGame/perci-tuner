#!/bin/sh

# @Author: Guocheng -- NUDT
#
# Build and run Perci-Tuner from one entry point.
# The script is intentionally POSIX-sh compatible because project examples call
# it as `sh scripts/run.sh ...`, which ignores the shebang.

set -e

###############################################################################
# Defaults
###############################################################################

MODE_FLAG=""
SOURCE_FILES=""
RUN_ARGS=""

# Precision / instrumentation flags.
MXP_FLAG=false
FPE_FLAG=false
CHECK_RESULT_FLAG=false
DD_FLAG=false
MIX_BINOP_FLAG=true

# Filters.
INCLUDE_FILE=""
INCLUDE_FUNC=""
EXCLUDE_FILE=""
EXCLUDE_FUNC=""

# Search / multi-version options.
RUN_ALGORITHM="dd"
THRESHOLD="1e-6"
REPEAT_TIMES="1"
TIMEOUT="1.5"
TIME_MODE="program"
HISTORY_FLAG=true
SHUFFLED_FLAG=false
BASELINE_TIME=""
BASELINE_RESULT=""

# Change-by-config options.
TARGET_CONFIG="changed_config.json"

# Multi-version options.
MULTI_VERSION_IR=""
MULTI_VERSION_FUNC=""
ORIGINAL_VERSION_FLAG=false

# Compiler/linker options.
BCS=""
HALF_PRECISION_SUPPORT="-Xclang -fnative-half-type"
FP128_SUPPORT=""
DD_SUPPORT=""

LINK_LIBS=""
LINK_DIRS=""
RUNTIME_LIB_DIRS=""
INCLUDE_DIRS=""
OPT_LEVEL="-O2" # Example: -O2. Beware that optimization may change results.

# run generate-config before search
REFRESH_CONFIG=true
NO_BUILD=false

# PERCI_ANALYSIS_OPTIONS="-Xclang -load -Xclang $PERCI_TUNER/build/libPerciAST.so -Xclang -plugin -Xclang perci-analysis "

###############################################################################
# Small helpers
###############################################################################

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '%s\n' "$*"
}

append_source_file() {
    SOURCE_FILES="${SOURCE_FILES:+$SOURCE_FILES }$1"
}

append_bitcode_file() {
    BCS="${BCS:+$BCS }$1"
}

append_run_arg() {
    RUN_ARGS="${RUN_ARGS:+$RUN_ARGS }$1"
}

append_link_lib() {
    LINK_LIBS="${LINK_LIBS:+$LINK_LIBS }$1"
}

append_link_dir() {
    LINK_DIRS="${LINK_DIRS:+$LINK_DIRS }$1"
}

append_runtime_lib_dir() {
    RUNTIME_LIB_DIRS="${RUNTIME_LIB_DIRS:+$RUNTIME_LIB_DIRS }$1"
}

append_include_dir() {
    INCLUDE_DIRS="${INCLUDE_DIRS:+$INCLUDE_DIRS }$1"
}

is_x86_64() {
    [ "$(uname -m)" = "x86_64" ]
}

cpu_count() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.ncpu
    else
        printf '1\n'
    fi
}

is_source_like_file() {
    case "$1" in
        *.c|*.cpp|*.h|*.hpp) return 0 ;;
        *) return 1 ;;
    esac
}

collect_source_files_from_dir() {
    dir="$1"

    [ -d "$dir" ] || return 0

    # The rest of this script follows the original whitespace-split behavior for
    # option lists, so paths containing spaces are still not supported.
    for found_file in $(find "$dir" -type f \( -name "*.c" -o -name "*.cpp" \) -print); do
        append_source_file "$found_file"
    done
}

require_perci_tuner_root() {
    [ -n "$PERCI_TUNER" ] || die "PERCI_TUNER is not set"
    [ -d "$PERCI_TUNER" ] || die "PERCI_TUNER does not point to a directory: $PERCI_TUNER"
}

show_help() {
    cat <<'EOF'
Usage:
  run.sh [mode] [options] src_files -l<library> -L<lib_path> -I<include_path> -O<level>

Without a mode, run.sh calls clang directly.

Available modes:
  generate-config         Generate config file (config.json) and executable
  generate-config-alpha   Generate config.json via pure Clang AST analysis
                          (merged .ast files; only basic floating-point vars:
                          local/global/params/struct members; no IR pipeline)
  search-optimize         Search for optimized config (run generate-config first)
  change-by-config        Modify source code precision according to target config
  change-by-config-alpha  Rewrite source (*.changed.c) via pure Clang AST analysis
                          (config-driven type token replacement; supports
                          local/global/params/struct members + multi-decl split)
  multi-version           Generate multi-version executable

Search options:
  -algorithm=exhaust/dd/pedd  Select search algorithm (default dd)
  -thres=<1e-6>               Set error threshold (default 1e-6)
  -repeat=<1>                 Set repeat count (default 1)
  -timeout=<1.5>              Set single program timeout multiplier (default 1.5 x baseline)
  -time-mode=program/kernel   Measure full program or stderr core_time_sec=<seconds> (default program)
  -shuffled                   Shuffle searched variables (default false)
  -history                    Keep search history (default true)
  -arg=<arg>                  Add one executable argument, in order
  -base-time=<time>           Manually set baseline running time
  -base-result=<result>       Manually set baseline result value
  -refresh-config             Refresh initial config file (default true)

Common options:
  -include-file=<file1,pat*>     Comma-separated source files to scan (default scan all)
  -include-func=<func1,pat*>     Comma-separated function names to scan (default scan all)
  -exclude-file=<file1,pat*>     Comma-separated source files to skip scanning
  -exclude-func=<func1,pat*>     Comma-separated function names to skip scanning
  -check-result               Print relative error in executable
  -fp128todd                  Treat fp128 as double-double
  -mix-binop=<true>           Enable mixed double/double-double binary operators
  -mxp                        Only include pragma precision and __autofp variables
  -fpe                        Print Floating-Point Exception, enables -mxp and -check-result

Change-by-config options:
  -target=<config>            Set target config file (default changed_config.json)

Multi-version options:
  -original-version           Use original version in multi-version mode (default false)
  -multi-ir=<ir>              Set another version IR
  -multi-func=<function>      Set multi-version function name

Examples:
  run.sh generate-config -mxp src.c -lm
  run.sh generate-config-alpha -Iinclude helper.c main.c
  run.sh change-by-config-alpha -target=config.json -Iinclude helper.c main.c
  run.sh search-optimize -mxp -algorithm=exhaust -thres=1e-5 -repeat=10 src.c -lm -lgsl -lblas
  run.sh change-by-config -target=config.json src.c
  run.sh multi-version -mxp -original-version -multi-ir=optimized.bc -multi-func=main src.c

__autofp usage (available with -mxp):
  __autofp is a flexible floating type for variable declarations. The actual
  precision is decided by the optimized result from search-optimize mode.
  Precision range: [fp16, float, double, long double]
  Example: __autofp var1, var2[100];

#pragma usage (available with -mxp):
  1. #pragma precision region
     Only variables and function calls in the following block are optimized.

  2. #pragma precision error result_var(1e-6)
     Specify the relative-error threshold for result_var while searching.
     This must be inserted in result_var's declaration scope.

  3. #pragma precision range(precisions0), var1(precisions1), var2(precisions2)
     Specify available precisions in a precision region.
     Available values: [__float128, double, float, __fp16]

  Example:
    void fun()
    {
        long double fun_result = 0.0;
        double t1, t2, s1;
        #pragma precision error fun_result(2.1e-7)
        #pragma precision region
        {
            #pragma precision range(__float128, double, float, __fp16), t2(double, float)
            t2 += 1.0;
            s1 = s1 + sqrt(h*h + (t2 - t1)*(t2 - t1));
            t1 = t2;
        }
        fun_result = t1;
    }
EOF
}

###############################################################################
# Build and preparation
###############################################################################

build_perci_tuner() {
    info "Build Perci-Tuner..."
    sh "$PERCI_TUNER/scripts/build.sh"
}

prepare_runtime_options() {
    if [ "$CHECK_RESULT_FLAG" = true ]; then
        append_link_dir "-L$PERCI_TUNER/lib/check_result"
        append_link_lib "-lcheck_result"
        append_runtime_lib_dir "-Wl,-rpath=$PERCI_TUNER/lib/check_result"
    fi

    if is_x86_64 && [ "$DD_FLAG" = true ]; then
        append_link_lib "-lquadmath"
        info "Link quadmath for x86_64 double-double support"
    fi

    if is_x86_64 && [ "$DD_FLAG" = true ]; then
        FP128_SUPPORT="-mlong-double-128"
        info "Enable float128 support for x86_64"
    fi
}

select_double_double_support_archive() {
    if is_x86_64; then
        DD_SUPPORT="$PERCI_TUNER/lib/dd_real/libautop-x86.a"
    else
        DD_SUPPORT="$PERCI_TUNER/lib/dd_real/libautop.a"
    fi
}

compile_sources_to_bitcode() {
    if [ -z "$SOURCE_FILES" ]; then
        if [ -f output.bc ]; then
            info "No source files supplied; reuse existing output.bc"
            return 0
        fi

        die "no source files supplied and output.bc does not exist"
    fi

    for src_file in $SOURCE_FILES; do
        [ -f "$src_file" ] || die "file not found: $src_file"

        if [ "$FPE_FLAG" = true ]; then
            clang $PERCI_ANALYSIS_OPTIONS -S -emit-llvm -g -gdwarf-2 -funwind-tables -mllvm -mxpfpe \
                "$src_file" $FP128_SUPPORT $INCLUDE_DIRS $HALF_PRECISION_SUPPORT \
                -fno-discard-value-names -o "$src_file.bc"
        else
            clang $PERCI_ANALYSIS_OPTIONS -g -emit-llvm -c \
                "$src_file" $FP128_SUPPORT $INCLUDE_DIRS $HALF_PRECISION_SUPPORT \
                -fno-discard-value-names -o "$src_file.bc"
        fi

        append_bitcode_file "$src_file.bc"
    done

    llvm-link $BCS -o output.bc
}

###############################################################################
# Mode implementations
###############################################################################

run_change_by_config() {
    opt output.bc -o output.bc

    PASSES="change_math_call,change_variable_decl"
    if [ "$DD_FLAG" = true ]; then
        PASSES="$PASSES,fp128_to_dd"
        select_double_double_support_archive
    fi

    opt -load-pass-plugin "$PERCI_TUNER/build/PerciTuner.so" -S \
        -passes="$PASSES" \
        --variable-config="$TARGET_CONFIG" \
        --function-config="$TARGET_CONFIG" \
        --include-file="$INCLUDE_FILE" \
        --include-func="$INCLUDE_FUNC" \
        --exclude-file="$EXCLUDE_FILE" \
        --exclude-func="$EXCLUDE_FUNC" \
        --enable-mix-binop="$MIX_BINOP_FLAG" \
        output.bc -o changed_output.bc

    clang $PERCI_ANALYSIS_OPTIONS $OPT_LEVEL -o changed_output changed_output.bc $LINK_LIBS $LINK_DIRS $DD_SUPPORT
    info "[Changed] IR: changed_output.bc, Executable: changed_output"
}

generate_config_and_original_executable() {
    if [ "$CHECK_RESULT_FLAG" = true ]; then
        opt -load-pass-plugin "$PERCI_TUNER/build/PerciTuner.so" -S \
            -passes="record_result,remove_pragma" \
            --is-original=true \
            output.bc -o mxp.bc

        clang $PERCI_ANALYSIS_OPTIONS $OPT_LEVEL -o output mxp.bc $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS
        rm mxp.bc

        LAST_LINE=$(./output | tail -n 1)
        export PERCI_TUNER_ORIGINAL_RESULT="$LAST_LINE"
        info "Set PERCI_TUNER_ORIGINAL_RESULT=$PERCI_TUNER_ORIGINAL_RESULT"
    fi

    PASSES="print_config,remove_pragma"
    if [ "$DD_FLAG" = true ] && [ "$MODE_FLAG" = "generate-config" ]; then
        PASSES="$PASSES,fp128_to_dd"
        select_double_double_support_archive
    fi

    opt -load-pass-plugin "$PERCI_TUNER/build/PerciTuner.so" -S \
        -passes="$PASSES" \
        --mxp="$MXP_FLAG" \
        --include-file="$INCLUDE_FILE" \
        --include-func="$INCLUDE_FUNC" \
        --exclude-file="$EXCLUDE_FILE" \
        --exclude-func="$EXCLUDE_FUNC" \
        --enable-mix-binop="$MIX_BINOP_FLAG" \
        output.bc -o output.bc
    # remove these to ensure optimzations available
    sed -i 's/\boptnone\b//g' output.bc
    sed -i 's/\bnoinline\b//g' output.bc
    clang $PERCI_ANALYSIS_OPTIONS $OPT_LEVEL -o output output.bc $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS $DD_SUPPORT
    info "[Original] Config: config.json, IR: output.bc, Executable: output"
}

run_generate_config_alpha() {
    # Split source files into main + helpers. A file literally named main.c /
    # main.cpp is treated as the merge target; everything else becomes an AST
    # that is merged into it. If no main.* is found, the last source file is
    # used as the main file.
    MAIN_FILE=""
    HELPER_FILES=""
    for src_file in $SOURCE_FILES; do
        base=$(basename "$src_file")
        case "$base" in
            main.c|main.cpp|main.cc)
                MAIN_FILE="$src_file"
                ;;
            *)
                HELPER_FILES="${HELPER_FILES:+$HELPER_FILES }$src_file"
                ;;
        esac
    done
    if [ -z "$MAIN_FILE" ]; then
        MAIN_FILE=$(printf '%s\n' $SOURCE_FILES | tail -n 1)
        HELPER_FILES=$(printf '%s\n' $SOURCE_FILES | head -n -1 | tr '\n' ' ' | sed 's/ $//')
    fi
    [ -n "$MAIN_FILE" ] || die "no source files supplied for generate-config-alpha"

    AST_DIR="$PERCI_TUNER/build/ast"
    mkdir -p "$AST_DIR"

    AST_MERGE_ARGS=""
    for helper in $HELPER_FILES; do
        ast_base=$(basename "$helper" | sed 's/\.[ch]\{1,2\}$//; s/\.cpp$//; s/\.cc$//')
        ast_file="$AST_DIR/${ast_base}.ast"
        info "Emit PCH: $helper -> $ast_file"
        clang -c -Xclang -emit-pch "$helper" $INCLUDE_DIRS $HALF_PRECISION_SUPPORT -o "$ast_file"
        AST_MERGE_ARGS="$AST_MERGE_ARGS -Xclang -ast-merge -Xclang $ast_file"
    done

    PERCI_AST_PLUGIN="$PERCI_TUNER/build/libPerciAST.so"
    [ -f "$PERCI_AST_PLUGIN" ] || die "libPerciAST.so not found at $PERCI_AST_PLUGIN"

    info "Run PerciAST plugin on merged AST (main: $MAIN_FILE)"
    clang -c -Xclang -load -Xclang "$PERCI_AST_PLUGIN" \
          -Xclang -plugin -Xclang perci-analysis \
          $AST_MERGE_ARGS \
          "$MAIN_FILE" $INCLUDE_DIRS $HALF_PRECISION_SUPPORT \
          -o "$AST_DIR/merged.o" > config.json

    info "[Alpha] Config: config.json (PerciAST plugin on merged AST; merged .ast files in $AST_DIR)"
}

run_change_by_config_alpha() {
    # Pure-AST source rewriting: loads libChangeVarAST.so on each source file,
    # which rewrites declaration type tokens per the target config and writes
    # *.changed.c per modified file. No IR pipeline, no recompilation.
    [ -f "$TARGET_CONFIG" ] || die "target config not found: $TARGET_CONFIG"

    PERCI_CHG_PLUGIN="$PERCI_TUNER/build/libChangeVarAST.so"
    [ -f "$PERCI_CHG_PLUGIN" ] || die "libChangeVarAST.so not found at $PERCI_CHG_PLUGIN"

    for src_file in $SOURCE_FILES; do
        [ -f "$src_file" ] || die "file not found: $src_file"
        info "Rewrite (AST): $src_file  (config: $TARGET_CONFIG)"
        clang -c -Xclang -load -Xclang "$PERCI_CHG_PLUGIN" \
              -Xclang -plugin -Xclang change-var-ast \
              -Xclang -plugin-arg-change-var-ast -Xclang "$TARGET_CONFIG" \
              "$src_file" $INCLUDE_DIRS -o /dev/null
    done

    info "[Alpha] Source rewriting done. Output: *.changed.c files in cwd"
}

run_search_optimize() {
    python3 -u "$PERCI_TUNER/scripts/search.py" \
        -algorithm="$RUN_ALGORITHM" \
        -thres="$THRESHOLD" \
        -repeat="$REPEAT_TIMES" \
        -timeout="$TIMEOUT" \
        -time-mode="$TIME_MODE" \
        -history="$HISTORY_FLAG" \
        -shuffled="$SHUFFLED_FLAG" \
        -mxp="$MXP_FLAG" \
        -fp128todd="$DD_FLAG" \
        output.bc \
        -mix-binop="$MIX_BINOP_FLAG" \
        $RUN_ARGS $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS $OPT_LEVEL \
        -base-time="$BASELINE_TIME" \
        -base-result="$BASELINE_RESULT"
}

run_multi_version() {
    if [ -z "$MULTI_VERSION_IR" ] || [ -z "$MULTI_VERSION_FUNC" ]; then
        die "multi-version mode requires -multi-ir=<ir_file> and -multi-func=<function_name>"
    fi

    export PERCI_TUNER_ORIGINAL_VERSION="$ORIGINAL_VERSION_FLAG"

    clang $PERCI_ANALYSIS_OPTIONS -S -emit-llvm -fno-discard-value-names \
        -o mvselect.bc "$PERCI_TUNER/lib/mvselect.c"

    llvm-link output.bc mvselect.bc -o output+mvselect.bc

    opt -load-pass-plugin "$PERCI_TUNER/build/PerciTuner.so" -S \
        -passes="merge_multi" \
        --ir-multi="$MULTI_VERSION_IR" \
        --function-multi="$MULTI_VERSION_FUNC" \
        --include-file="$INCLUDE_FILE" \
        --include-func="$INCLUDE_FUNC" \
        --exclude-file="$EXCLUDE_FILE" \
        --exclude-func="$EXCLUDE_FUNC" \
        --enable-mix-binop="$MIX_BINOP_FLAG" \
        output+mvselect.bc -o merged.bc

    clang $PERCI_ANALYSIS_OPTIONS $OPT_LEVEL -o merged merged.bc $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS
    info "[Merged] IR: merged.bc, Executable: merged"
}

run_fpe_postprocess() {
    [ "$FPE_FLAG" = true ] || return 0

    clang $PERCI_ANALYSIS_OPTIONS $OPT_LEVEL -o fpe_opt -rdynamic -funwind-tables optimized_output.bc \
        -lm $LINK_LIBS $LINK_DIRS $RUNTIME_LIB_DIRS
}

###############################################################################
# Argument parsing
###############################################################################

for arg in "$@"; do
    case "$arg" in
        -h|--help)
            show_help
            exit 0
            ;;
        generate-config|generate-config-alpha|search-optimize|change-by-config|change-by-config-alpha|multi-version)
            MODE_FLAG="$arg"
            ;;
        -no-build)
            NO_BUILD=true
            ;;
        -refresh-config=*)
            REFRESH_CONFIG="${arg#-refresh-config=}"
            ;;
        -include-file=*)
            INCLUDE_FILE="${arg#-include-file=}"
            ;;
        -include-func=*)
            INCLUDE_FUNC="${arg#-include-func=}"
            ;;
        -exclude-file=*)
            EXCLUDE_FILE="${arg#-exclude-file=}"
            ;;
        -exclude-func=*)
            EXCLUDE_FUNC="${arg#-exclude-func=}"
            ;;
        -algorithm=*)
            RUN_ALGORITHM="${arg#-algorithm=}"
            ;;
        -mix-binop=*)
            MIX_BINOP_FLAG="${arg#-mix-binop=}"
            ;;
        -shuffled)
            SHUFFLED_FLAG=true
            ;;
        -history)
            HISTORY_FLAG=true
            ;;
        -check-result)
            CHECK_RESULT_FLAG=true
            ;;
        -mxp)
            MXP_FLAG=true
            CHECK_RESULT_FLAG=true
            ;;
        -fp128todd)
            DD_FLAG=true
            ;;
        -fpe)
            FPE_FLAG=true
            MXP_FLAG=true
            CHECK_RESULT_FLAG=true
            ;;
        -original-version)
            ORIGINAL_VERSION_FLAG=true
            ;;
        -target=*)
            TARGET_CONFIG="${arg#-target=}"
            ;;
        -multi-ir=*)
            MULTI_VERSION_IR="${arg#-multi-ir=}"
            ;;
        -multi-func=*)
            MULTI_VERSION_FUNC="${arg#-multi-func=}"
            ;;
        -thres=*)
            THRESHOLD="${arg#-thres=}"
            ;;
        -repeat=*)
            REPEAT_TIMES="${arg#-repeat=}"
            ;;
        -timeout=*)
            TIMEOUT="${arg#-timeout=}"
            ;;
        -time-mode=*)
            TIME_MODE="${arg#-time-mode=}"
            case "$TIME_MODE" in
                program|kernel) ;;
                *) die "-time-mode must be 'program' or 'kernel'" ;;
            esac
            ;;
        -base-time=*)
            BASELINE_TIME="${arg#-base-time=}"
            ;;
        -base-result=*)
            BASELINE_RESULT="${arg#-base-result=}"
            ;;
        -arg*)
            append_run_arg "$arg"
            ;;
        -l*)
            append_link_lib "$arg"
            ;;
        -L*)
            append_link_dir "$arg"
            ;;
        -I*)
            append_include_dir "$arg"
            ;;
        -O*)
            OPT_LEVEL="$arg"
            ;;
        *)
            if [ -d "$arg" ]; then
                collect_source_files_from_dir "$arg"
            elif [ -f "$arg" ] && is_source_like_file "$arg"; then
                append_source_file "$arg"
            fi
            ;;
    esac
done

###############################################################################
# Entry point
###############################################################################

if [ -z "$MODE_FLAG" ]; then
    info "call clang..."
    clang "$@"
    exit $?
fi

# generate-config-alpha and change-by-config-alpha are pure-AST paths: they
# do not touch the LLVM IR pipeline (opt / PerciTuner.so), so they bypass
# build_perci_tuner and compile_sources_to_bitcode.
if [ "$MODE_FLAG" = "generate-config-alpha" ] || [ "$MODE_FLAG" = "change-by-config-alpha" ]; then
    require_perci_tuner_root
    if [ "$NO_BUILD" = false ]; then
        info "Build AST plugins (alpha mode)..."
        mkdir -p "$PERCI_TUNER/build"
        ( cd "$PERCI_TUNER/build" && ninja PerciAST ChangeVarAST ) || {
            ( cd "$PERCI_TUNER/build" && cmake -G Ninja .. && ninja PerciAST ChangeVarAST )
        }
    fi
    clang --version
    case "$MODE_FLAG" in
        generate-config-alpha)    run_generate_config_alpha ;;
        change-by-config-alpha)   run_change_by_config_alpha ;;
    esac
    exit 0
fi

require_perci_tuner_root
[ "$NO_BUILD" = false ] && build_perci_tuner
prepare_runtime_options

clang --version
compile_sources_to_bitcode

case "$MODE_FLAG" in
    change-by-config)
        run_change_by_config
        ;;
    generate-config)
        generate_config_and_original_executable
        ;;
    search-optimize|multi-version)
        if [ "$REFRESH_CONFIG" = true ]; then
            generate_config_and_original_executable
        fi
        ;;
    *)
        die "unknown mode: $MODE_FLAG"
        ;;
esac

if [ "$MODE_FLAG" = "search-optimize" ]; then
    run_search_optimize
fi

if [ "$MODE_FLAG" = "multi-version" ]; then
    run_multi_version
fi

run_fpe_postprocess
