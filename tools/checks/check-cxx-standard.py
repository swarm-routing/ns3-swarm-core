#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""core/ keeps up with the C++ standard of the ns-3 release swarm-core supports.

    check-cxx-standard.py [--ns3 /opt/ns-3] [core/CMakeLists.txt]
    check-cxx-standard.py --self-test

The rule (docs/api-policy.md "C++ standard", #141): core/ targets the standard
ns-3 itself requires, its CMAKE_CXX_STANDARD_MINIMUM in
build-support/macros-and-definitions.cmake (C++23 from ns-3.46). This reads
that minimum from an ns-3 tree (in CI, the ns3-dev image's /opt/ns-3) and
fails when core/CMakeLists.txt's `set(CMAKE_CXX_STANDARD N)`, or any
`cxx_std_N` compile feature it gives its target (what a consumer inherits), is
below it, or when the two disagree. A standard above ns-3's minimum passes:
the rule is a floor, so swarm-core may move first. Exit 0 when it holds, 1
when it does not, 2 on a file it cannot read.
"""

import re
import sys
import tempfile
from pathlib import Path

MINIMUM = re.compile(r"^\s*set\(CMAKE_CXX_STANDARD_MINIMUM\s+(\d+)\s*\)", re.M)
STANDARD = re.compile(r"^\s*set\(CMAKE_CXX_STANDARD\s+(\d+)\s*\)", re.M)
FEATURE = re.compile(r"\bcxx_std_(\d+)\b")


def uncommented(text: str) -> str:
    """CMake text without its line comments, so a commented-out line counts for nothing."""
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def ns3_minimum(ns3: Path) -> int:
    macros = ns3 / "build-support" / "macros-and-definitions.cmake"
    found = MINIMUM.findall(macros.read_text())
    if not found:
        raise ValueError(f"{macros}: no set(CMAKE_CXX_STANDARD_MINIMUM N)")
    return int(found[0])


def core_standards(cmakelists: Path) -> tuple[int, list[int]]:
    text = uncommented(cmakelists.read_text())
    found = STANDARD.findall(text)
    if len(found) != 1:
        raise ValueError(
            f"{cmakelists}: expected one set(CMAKE_CXX_STANDARD N), found {len(found)}"
        )
    return int(found[0]), [int(n) for n in FEATURE.findall(text)]


def problems(minimum: int, standard: int, features: list[int]) -> list[str]:
    out = []
    if standard < minimum:
        out.append(f"CMAKE_CXX_STANDARD {standard} is below ns-3's minimum C++{minimum}")
    if not features:
        out.append("no cxx_std_N compile feature: a consumer would not inherit the standard")
    for n in features:
        if n != standard:
            out.append(f"cxx_std_{n} differs from CMAKE_CXX_STANDARD {standard}")
    return out


def check(ns3: Path, cmakelists: Path) -> int:
    try:
        minimum = ns3_minimum(ns3)
        standard, features = core_standards(cmakelists)
    except (OSError, ValueError) as e:
        print(f"check-cxx-standard: {e}", file=sys.stderr)
        return 2
    bad = problems(minimum, standard, features)
    for p in bad:
        print(f"check-cxx-standard: FAIL: {cmakelists}: {p}", file=sys.stderr)
    if bad:
        print(
            "check-cxx-standard: core/ must target the C++ standard of the ns-3 release swarm-core "
            "supports (docs/api-policy.md); raise it in a minor release, with an issue in each consumer",
            file=sys.stderr,
        )
        return 1
    print(f"check-cxx-standard: OK: core/ at C++{standard}, ns-3 requires C++{minimum}")
    return 0


def self_test() -> int:
    def tree(root: Path, minimum: str | None, core: str) -> tuple[Path, Path]:
        ns3 = root / "ns-3"
        (ns3 / "build-support").mkdir(parents=True)
        macros = "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        if minimum is not None:
            macros = f"  set(CMAKE_CXX_STANDARD_MINIMUM {minimum})\n" + macros
        (ns3 / "build-support" / "macros-and-definitions.cmake").write_text(macros)
        cm = root / "CMakeLists.txt"
        cm.write_text(core)
        return ns3, cm

    def core(std: str, feature: str | None) -> str:
        text = f"project(x)\nset(CMAKE_CXX_STANDARD {std})\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        if feature is not None:
            text += f"target_compile_features(swarm_core PUBLIC cxx_std_{feature})\n"
        return text

    cases = [
        ("equal", "23", core("23", "23"), 0),
        ("above the minimum", "20", core("23", "23"), 0),
        ("below the minimum", "23", core("20", "20"), 1),
        ("feature below the standard", "23", core("23", "20"), 1),
        ("no feature", "23", core("23", None), 1),
        ("ns-3 declares no minimum", None, core("23", "23"), 2),
        ("two standards in core/", "23", core("23", "23") + "set(CMAKE_CXX_STANDARD 20)\n", 2),
        ("a feature in a comment is not one", "23", "# cxx_std_20\n" + core("23", "23"), 0),
        (
            "a commented-out feature is missing",
            "23",
            core("23", None) + "# target_compile_features(x PUBLIC cxx_std_23)\n",
            1,
        ),
        (
            "a standard in a comment is not one",
            "23",
            "# set(CMAKE_CXX_STANDARD 17)\n" + core("23", "23"),
            0,
        ),
    ]
    failed = 0
    for name, minimum, text, want in cases:
        with tempfile.TemporaryDirectory() as d:
            ns3, cm = tree(Path(d), minimum, text)
            got = check(ns3, cm)
        if got != want:
            print(f"self-test FAIL: {name}: exit {got}, want {want}", file=sys.stderr)
            failed += 1
    # The real file passes against a C++23 ns-3.
    real = Path(__file__).resolve().parents[2] / "core" / "CMakeLists.txt"
    with tempfile.TemporaryDirectory() as d:
        ns3, _ = tree(Path(d), "23", "")
        if check(ns3, real) != 0:
            print("self-test FAIL: core/CMakeLists.txt against ns-3 C++23", file=sys.stderr)
            failed += 1
    print(f"check-cxx-standard self-test: {len(cases) + 1 - failed}/{len(cases) + 1} passed")
    return 1 if failed else 0


def main(argv: list[str]) -> int:
    if argv == ["--self-test"]:
        return self_test()
    ns3 = Path("/opt/ns-3")
    if argv[:1] == ["--ns3"] and len(argv) >= 2:
        ns3, argv = Path(argv[1]), argv[2:]
    if len(argv) > 1 or (argv and argv[0].startswith("-")):
        print(__doc__, file=sys.stderr)
        return 2
    cmakelists = (
        Path(argv[0]) if argv else Path(__file__).resolve().parents[2] / "core" / "CMakeLists.txt"
    )
    return check(ns3, cmakelists)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
