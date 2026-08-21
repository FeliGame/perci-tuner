#!/bin/sh

# Build Perci-Tuner using the Ninja generator.
#   -r        Force a clean rebuild (ninja -t clean first)
#   -j N      Use N parallel jobs (default: nproc)

set -e

FORCE=0
JOBS=""

while [ $# -gt 0 ]; do
    case "$1" in
        -r) FORCE=1 ;;
        -j) shift; JOBS="$1" ;;
        -j*) JOBS="${1#-j}" ;;
        *) printf 'build.sh: unknown argument: %s\n' "$1" >&2; exit 1 ;;
    esac
    shift
done

[ -n "$JOBS" ] || JOBS="$(nproc 2>/dev/null || echo 1)"

[ -n "$PERCI_TUNER" ] || { printf 'PERCI_TUNER is not set\n' >&2; exit 1; }
cd "$PERCI_TUNER"
mkdir -p build
cd build

[ -f build.ninja ] || cmake -G Ninja ..

[ "$FORCE" = 1 ] && ninja -t clean
ninja -j "$JOBS"
