# lit configuration for the Perci-Tuner test suite.
#
# A test "passes" when its RUN lines all succeed; each .c benchmark carries
# its own directives in a header comment block (run the tuner, scan output
# for error markers).

import os

import lit.formats

# Project root = parent of this test directory.
config.test_source_root = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.dirname(config.test_source_root)

config.name = "PerciTuner"
config.test_format = lit.formats.ShTest(True)

# Treat .c files as lit test cases; each benchmark carries its own RUN/CHECK
# directives in a header comment block.
config.suffixes = [".c"]

# Helper .c files (no main, no lit directives) would be discovered as empty
# tests and fail with "Test has no 'RUN:' line".  Dynamically collect their
# filenames so lit skips them.
_helper_names = set()
for _dp, _dns, _fns in os.walk(config.test_source_root):
    _dns[:] = [d for d in _dns if not d.startswith(".") and d != "Output" and d != "sys"]
    for _fn in _fns:
        if not _fn.endswith(".c"):
            continue
        with open(os.path.join(_dp, _fn)) as _f:
            _head = _f.read(256)
        if "// RUN:" not in _head:
            _helper_names.add(_fn)
# NPB benchmarks ship a "sys" directory with build utilities (e.g.
# setparams.c); those are not tuner test cases.
config.excludes = sorted(_helper_names | {"sys"})

# ShTest cases are shell-based; advertise the feature so `REQUIRES: shell`
# guards evaluate as available.
config.available_features.add("shell")

# Run tests in place so %t lives next to the test file.
config.test_exec_root = config.test_source_root

# PERCI_TUNER is required by scripts/run.sh. Prefer the environment so users
# get the same value they set in ~/.bashrc; otherwise fall back to the source
# tree this suite ships in.
perci_tuner = os.environ.get("PERCI_TUNER") or project_root
config.environment["PERCI_TUNER"] = perci_tuner
config.substitutions.append(("%PERCI_TUNER", perci_tuner))

config.timeout = 240
