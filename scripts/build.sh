#!/bin/sh

#   -r        Force a clean rebuild
#   -j N      Use N parallel jobs (default: nproc)
#   -cir      Enable CIR/MLIR support (requires LLVM built with mlir)

set -e

FORCE=0
JOBS=""
CIR_FLAG=0

while [ $# -gt 0 ]; do
    case "$1" in
        -r) FORCE=1 ;;
        -j) shift; JOBS="$1" ;;
        -j*) JOBS="${1#-j}" ;;
        -cir) CIR_FLAG=1 ;;
        *) printf 'build.sh: unknown argument: %s\n' "$1" >&2; exit 1 ;;
    esac
    shift
done

[ -n "$JOBS" ] || JOBS="$(nproc 2>/dev/null || echo 1)"

[ -n "$PERCI_TUNER" ] || { printf 'PERCI_TUNER is not set\n' >&2; exit 1; }
cd "$PERCI_TUNER"
mkdir -p build
cd build

# Configure once. Prefer Ninja when available; otherwise fall back to the
# default generator (Unix Makefiles).
if [ ! -f build.ninja ] && [ ! -f Makefile ]; then
    CMAKE_EXTRA_ARGS=""
    if [ "$CIR_FLAG" = 1 ]; then
        CMAKE_EXTRA_ARGS="-DPERCI_ENABLE_CIR=ON"
    fi
    if command -v ninja >/dev/null 2>&1; then
        cmake -G Ninja $CMAKE_EXTRA_ARGS ..
    else
        cmake $CMAKE_EXTRA_ARGS ..
    fi
fi

# Match the build tool to the configured generator.
if [ -f build.ninja ]; then
    BUILD_TOOL=ninja
    CLEAN_CMD="ninja -t clean"
else
    BUILD_TOOL=make
    CLEAN_CMD="make clean"
fi

[ "$FORCE" = 1 ] && $CLEAN_CMD

$BUILD_TOOL -j "$JOBS"
