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
# Then the same consumer with -DSWARM_CORE_SANITIZE=ON (in BUILD-DIR/sanitized):
# it must link, which needs the sanitizer runtime on the consumer's link line,
# and its test must pass.
#
# Then core/ as a released package (#133, #134), with core/test/consumer-package:
#
#   - installed (`cmake --install` of core/ alone into BUILD-DIR/prefix): the
#     library, the headers under include/swarm-core/ (testbench/ included) and
#     lib/cmake/swarm-core/; `find_package(swarm-core <VERSION> CONFIG REQUIRED)`
#     finds it and the consumer links the imported swarm::core and passes; its
#     own major.minor is accepted, another major (and, before 1.0, another
#     minor) refused;
#   - fetched as the release asset: the tarball tools/release/dist.sh builds
#     (DIST_CHECK=0), through FetchContent URL + URL_HASH SHA256 with
#     SOURCE_SUBDIR core, the way a consumer's release lock uses it.
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
build=$(mkdir -p "$build" && cd "$build" && pwd)  # absolute: a prefix and a file:// URL below
fail() { echo "check-core-consumer: FAIL: $*" >&2; exit 1; }

cmake -S "$repo/core/test/consumer" -B "$build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
cmake --build "$build" -j "$(nproc)" >/dev/null

targets=$(cmake --build "$build" --target help)
grep -q 'swarm_core_tests' <<<"$targets" && fail "the consumer's build has the target swarm_core_tests"
grep -Eq '(^|[ /])swarm_core( |$)' <<<"$targets" || fail "the consumer's build has no swarm_core target"

listed=$(ctest --test-dir "$build" -N)
# A test entry named swarm-core, not the name anywhere (the first line of the
# listing is the build directory, which in CI is under .../ns3-swarm-core/).
grep -Eq '^ *Test +#[0-9]+: swarm-core$' <<<"$listed" && fail "the consumer's ctest lists swarm-core's suite"
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

cmake -S "$repo/core/test/consumer" -B "$build/sanitized" -DSWARM_CORE_SANITIZE=ON >/dev/null
cmake --build "$build/sanitized" -j "$(nproc)" >/dev/null ||
    fail "the consumer does not build with SWARM_CORE_SANITIZE=ON"
ctest --test-dir "$build/sanitized" --output-on-failure >/dev/null ||
    fail "the consumer's test failed with SWARM_CORE_SANITIZE=ON"
# --- the installed package (#133) --------------------------------------------
ver=$(cat "$repo/VERSION")
IFS=. read -r major minor _ <<<"$ver"
cmake -S "$repo/core" -B "$build/core" -DSWARM_CORE_TESTS=OFF >/dev/null
cmake --build "$build/core" -j "$(nproc)" >/dev/null
cmake --install "$build/core" --prefix "$build/prefix" >/dev/null
for f in include/swarm-core/swarm-ports.h include/swarm-core/testbench/swarm-testbench.h \
         lib/cmake/swarm-core/swarm-coreConfig.cmake lib/cmake/swarm-core/swarm-coreConfigVersion.cmake \
         lib/cmake/swarm-core/swarm-coreTargets.cmake; do
    [ -f "$build/prefix/$f" ] || fail "the install has no $f"
done
compgen -G "$build/prefix/lib*/libswarm_core.a" >/dev/null || fail "the install has no libswarm_core.a"

pkg=$repo/core/test/consumer-package
cmake -S "$pkg" -B "$build/package" -DCMAKE_PREFIX_PATH="$build/prefix" -DSWARM_CORE_WANT="$ver" >/dev/null ||
    fail "find_package(swarm-core $ver CONFIG) does not find the install"
cmake --build "$build/package" -j "$(nproc)" >/dev/null || fail "the package consumer does not build"
ctest --test-dir "$build/package" --output-on-failure >/dev/null || fail "the package consumer's test failed"
# SameMajorVersion from 1.0, SameMinorVersion before it (docs/api-policy.md):
# the release's own major.minor is accepted, a newer one and an older major (or,
# before 1.0, an older minor) refused.
cmake -S "$pkg" -B "$build/accept" -DCMAKE_PREFIX_PATH="$build/prefix" \
    -DSWARM_CORE_WANT="$major.$minor" >/dev/null || fail "find_package(swarm-core $major.$minor CONFIG) refused $ver"
refuse=("$((major + 1)).0")
if [ "$major" = 0 ]; then
    refuse+=("0.$((minor + 1))")
    [ "$minor" -gt 0 ] && refuse+=("0.$((minor - 1))")
else
    refuse+=("$((major - 1)).0")
fi
for want in "${refuse[@]}"; do
    if cmake -S "$pkg" -B "$build/refuse-$want" -DCMAKE_PREFIX_PATH="$build/prefix" \
            -DSWARM_CORE_WANT="$want" >/dev/null 2>&1; then
        fail "find_package(swarm-core $want CONFIG) accepted $ver"
    fi
done

# --- the release tarball through FetchContent URL + URL_HASH (#134) -----------
DIST_CHECK=0 "$repo/tools/release/dist.sh" "$ver" "$build/dist" >/dev/null
tarball=$build/dist/swarm-core-$ver.tar.gz
sha=$(sha256sum "$tarball" | cut -d' ' -f1)
cmake -S "$pkg" -B "$build/url" -DSWARM_CORE_FROM=url -DSWARM_CORE_URL="file://$tarball" \
    -DSWARM_CORE_SHA256="$sha" >/dev/null || fail "FetchContent of the release tarball failed"
cmake --build "$build/url" -j "$(nproc)" >/dev/null || fail "the URL consumer does not build"
ctest --test-dir "$build/url" --output-on-failure >/dev/null || fail "the URL consumer's test failed"
if cmake -S "$pkg" -B "$build/badhash" -DSWARM_CORE_FROM=url -DSWARM_CORE_URL="file://$tarball" \
        -DSWARM_CORE_SHA256="$(printf '0%.0s' {1..64})" >/dev/null 2>&1; then
    fail "FetchContent accepted the tarball under a wrong URL_HASH"
fi

echo "check-core-consumer: ok (links swarm::core, also with SWARM_CORE_SANITIZE=ON; no swarm_core_tests target, no swarm-core ctest entry, no -Werror; find_package(swarm-core $ver) from an install, ${refuse[*]} refused; the release tarball by URL + URL_HASH)"
