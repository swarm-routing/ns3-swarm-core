#!/usr/bin/env bash
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# dist.sh VERSION OUT-DIR: the release asset (epic #6), as `make dist` runs it
# for swarm-routing/.github's reusable-release.yml:
#
#   swarm-core-VERSION.tar.gz   the ns-3 contrib module, ready for contrib/:
#                               swarm-core/ with CMakeLists.txt, core/ (the
#                               dual-licensed primitives, with core/LICENSE;
#                               also builds alone with CMake), model/, helper/,
#                               test/, examples/, docs/, README.md, LICENSE,
#                               LICENSE-MIT, LICENSING.md, VERSION, INSTALL.txt
#
# Deterministic: only files tracked by git, entries sorted with a fixed owner
# and the release commit's time, gzip -n.
#
# Then it checks what it built, so a release never ships a package that does
# not work (the workflow stops before signing or tagging on a non-zero exit):
# the tarball, unpacked into a clean ns3-dev tree's contrib/, builds with
# strict warnings and passes `test.py -s swarm-core`, and its core/ builds
# alone (plain CMake, no ns-3) and passes ctest (docker; NS3_IMAGE).
# DIST_CHECK=0 skips the check (a quick local build).
set -euo pipefail
ver=${1:?usage: dist.sh VERSION OUT-DIR}
out=${2:?usage: dist.sh VERSION OUT-DIR}
NS3_IMAGE=${NS3_IMAGE:-ghcr.io/swarm-routing/ns3-dev:latest-debug}
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
epoch=$(git -C "$repo" log -1 --format=%ct)

[ "$(cat "$repo/VERSION")" = "$ver" ] ||
    { echo "dist.sh: VERSION is $(cat "$repo/VERSION"), not $ver" >&2; exit 1; }

# --- swarm-core-VERSION.tar.gz ----------------------------------------------
pkg="$tmp/pkg/swarm-core"
mkdir -p "$pkg"
git -C "$repo" ls-files -z -- CMakeLists.txt core helper model test examples docs \
        README.md LICENSE LICENSE-MIT LICENSING.md VERSION |
    (cd "$repo" && xargs -0 cp --parents -t "$pkg")
cat > "$pkg/INSTALL.txt" <<TXT
swarm-core $ver (swarm-routing/ns3-swarm-core): the shared ns-3 contrib
module every swarm-routing protocol links against. core/ is MIT OR
GPL-2.0-only (core/LICENSE); the rest is GPL-2.0-only (LICENSING.md).
Needs ns-3.42 or later, built at C++20 or later.

  tar -xzf swarm-core-$ver.tar.gz -C /path/to/ns-3/contrib
  cd /path/to/ns-3
  ./ns3 configure --enable-examples --enable-tests && ./ns3 build
  ./test.py -s swarm-core

A protocol module then links it (LIBRARIES_TO_LINK \${libswarm-core}) and
includes "ns3/swarm-core-module.h". core/ alone, with no ns-3:

  cmake -S swarm-core/core -B build && cmake --build build && ctest --test-dir build

Source: https://github.com/swarm-routing/ns3-swarm-core/tree/v$ver
TXT
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@${epoch}" \
    -C "$tmp/pkg" -cf - swarm-core | gzip -n > "$out/swarm-core-$ver.tar.gz"
ls -l "$out"

[ "${DIST_CHECK:-1}" = 0 ] && { echo "dist.sh: DIST_CHECK=0, the package is not checked"; exit 0; }

# --- check: the tarball builds in a clean ns3-dev tree and passes its suite ---
mkdir -p "$tmp/contrib"
tar -xzf "$out/swarm-core-$ver.tar.gz" -C "$tmp/contrib"
docker run --rm -v "$tmp/contrib:/contrib:ro" "$NS3_IMAGE" bash -euo pipefail -c '
    ns3-build-contrib --strict-warnings --test swarm-core=/contrib/swarm-core
    cmake -S /contrib/swarm-core/core -B /tmp/core-build
    cmake --build /tmp/core-build -j "$(nproc)"
    ctest --test-dir /tmp/core-build --output-on-failure'
echo "dist.sh: swarm-core-$ver.tar.gz builds in $NS3_IMAGE, passes test.py -s swarm-core, and its core/ passes ctest alone"
