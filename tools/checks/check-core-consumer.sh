#!/usr/bin/env bash
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# check-core-consumer.sh [BUILD-DIR]: core/ used from another CMake project
# (#100). Builds the scratch consumer in core/test/consumer, which adds core/
# as a subdirectory and links swarm::core, then checks what a consumer must
# not inherit from swarm-core's own build:
#
#   - no swarm_core_tests target, and no `swarm-core` entry in its ctest;
#   - no -Werror on its own sources, nor on swarm-core's inside its build;
#   - its own test (the consumer program, linked against the library) passes.
#
# CI runs it in the `core` check (.github/workflows/core.yml). Needs cmake, a
# C++20 compiler and python3; no ns-3.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:-}
if [ -z "$build" ]; then
    build=$(mktemp -d)
    trap 'rm -rf "$build"' EXIT
fi
fail() { echo "check-core-consumer: FAIL: $*" >&2; exit 1; }

cmake -S "$repo/core/test/consumer" -B "$build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
cmake --build "$build" -j "$(nproc)" >/dev/null

targets=$(cmake --build "$build" --target help)
grep -q 'swarm_core_tests' <<<"$targets" && fail "the consumer's build has the target swarm_core_tests"
grep -Eq '(^|[ /])swarm_core( |$)' <<<"$targets" || fail "the consumer's build has no swarm_core target"

listed=$(ctest --test-dir "$build" -N)
grep -q 'swarm-core' <<<"$listed" && fail "the consumer's ctest lists swarm-core's suite"
grep -q 'Total Tests: 1$' <<<"$listed" || fail "the consumer's ctest should list its one test: $listed"

python3 -I - "$build/compile_commands.json" <<'PY' || fail "a compile command carries -Werror"
import json
import sys

commands = json.load(open(sys.argv[1]))
files = sorted(c["file"].rsplit("/", 1)[-1] for c in commands)
assert "consumer.cc" in files and "swarm-selection.cc" in files, files
bad = [c["file"] for c in commands if "-Werror" in c["command"].split()]
for name in bad:
    print(f"-Werror on {name}", file=sys.stderr)
sys.exit(1 if bad else 0)
PY

ctest --test-dir "$build" --output-on-failure >/dev/null || fail "the consumer's test failed"
echo "check-core-consumer: ok (links swarm::core; no swarm_core_tests target, no swarm-core ctest entry, no -Werror)"
