#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""The core microbenchmarks of a PR head against its base (#170).

Reads Google Benchmark JSON files (``--benchmark_out_format=json``) written by
the base build and by the head build on the same runner, takes the median CPU
time per benchmark over every repetition of every file of a side, and prints
the delta table as Markdown. A benchmark more than ``--threshold`` percent
slower on the head gets a GitHub ``::warning::`` line (on stdout, which the
runner turns into an annotation). Report-only: the exit status is 0 whatever
the numbers, because shared runners are noisy; it is non-zero only when the
input cannot be read.

    bench_compare.py compare --base base-*.json --head head-*.json \\
        [--threshold 15] [--markdown delta.md] [--title TEXT]
    bench_compare.py --self-test

A benchmark only one side has is listed as ``new`` or ``removed``; its row
has no delta. Each side runs its own ``core/bench/`` source, so a PR that
changes a benchmark's body changes what that row measures: the table says so
only through the PR's diff.
"""

import argparse
import contextlib
import io
import json
import statistics
import sys
import tempfile
from pathlib import Path

UNIT_NS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}


def load(paths):
    """{benchmark name: [cpu ns, ...]} over every repetition of every file."""
    times = {}
    for path in paths:
        try:
            data = json.loads(Path(path).read_text())
        except (OSError, ValueError) as e:
            sys.exit(f"bench_compare: cannot read {path}: {e}")
        for b in data.get("benchmarks", []):
            if b.get("run_type", "iteration") != "iteration" or b.get("error_occurred"):
                continue
            name = b.get("run_name", b.get("name"))
            scale = UNIT_NS.get(b.get("time_unit", "ns"))
            if name is None or scale is None or "cpu_time" not in b:
                sys.exit(f"bench_compare: {path}: unreadable entry {b!r}")
            times.setdefault(name, []).append(float(b["cpu_time"]) * scale)
    return times


def fmt_ns(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.2f} us"
    return f"{ns:.1f} ns"


def compare(base, head, threshold):
    """Rows (name, base ns|None, head ns|None, delta %|None, slower?) in name order."""
    rows = []
    for name in sorted(set(base) | set(head)):
        b = statistics.median(base[name]) if name in base else None
        h = statistics.median(head[name]) if name in head else None
        delta = (h / b - 1.0) * 100.0 if b and h is not None else None
        rows.append((name, b, h, delta, delta is not None and delta > threshold))
    return rows


def markdown(rows, threshold, title, nbase, nhead):
    slower = [r[0] for r in rows if r[4]]
    out = [f"## {title}", ""]
    out.append(
        f"Median CPU time per operation over {nbase} base and {nhead} head sample(s) "
        f"per benchmark, same runner. Report-only; more than +{threshold:g} % is flagged."
    )
    out.append("")
    if slower:
        out.append(
            f"**{len(slower)} slower than +{threshold:g} %:** "
            + ", ".join(f"`{n}`" for n in slower)
        )
    else:
        out.append(f"Nothing slower than +{threshold:g} %.")
    out += ["", "| benchmark | base | head | delta | |", "|---|---:|---:|---:|---|"]
    for name, b, h, delta, flag in rows:
        if b is None:
            out.append(f"| `{name}` | | {fmt_ns(h)} | | new |")
        elif h is None:
            out.append(f"| `{name}` | {fmt_ns(b)} | | | removed |")
        else:
            out.append(
                f"| `{name}` | {fmt_ns(b)} | {fmt_ns(h)} | {delta:+.1f} % | {'slower' if flag else ''} |"
            )
    return "\n".join(out) + "\n"


def run_compare(args):
    base = load(args.base)
    head = load(args.head)
    if not head:
        sys.exit("bench_compare: no head benchmarks")
    rows = compare(base, head, args.threshold)
    nbase = max((len(v) for v in base.values()), default=0)
    nhead = max((len(v) for v in head.values()), default=0)
    md = markdown(rows, args.threshold, args.title, nbase, nhead)
    if args.markdown:
        Path(args.markdown).write_text(md)
    else:
        print(md, end="")
    for name, b, h, delta, flag in rows:
        if flag:
            print(
                f"::warning title=core benchmark slower::{name}: {fmt_ns(b)} -> {fmt_ns(h)} "
                f"({delta:+.1f} %, threshold +{args.threshold:g} %)"
            )
    return 0


def self_test():
    def entry(name, cpu, unit="ns", rep=0, run_type="iteration"):
        return {
            "name": name,
            "run_name": name,
            "run_type": run_type,
            "repetition_index": rep,
            "cpu_time": cpu,
            "real_time": cpu,
            "time_unit": unit,
        }

    with tempfile.TemporaryDirectory() as d:
        dd = Path(d)

        def write(fname, entries):
            p = dd / fname
            p.write_text(json.dumps({"context": {}, "benchmarks": entries}))
            return str(p)

        base = [
            write(
                "base-1.json",
                [
                    entry("BM_A", 100),
                    entry("BM_A", 102, rep=1),
                    entry("BM_B", 1.0, "us"),
                    entry("BM_Gone", 5),
                    entry("BM_A_median", 1, run_type="aggregate"),
                ],
            ),
            write("base-2.json", [entry("BM_A", 98), entry("BM_B", 1000), entry("BM_Gone", 5)]),
        ]
        head = [
            write(
                "head-1.json",
                [
                    entry("BM_A", 120),
                    entry("BM_A", 121, rep=1),
                    entry("BM_B", 1100),
                    entry("BM_New", 7),
                ],
            ),
            write(
                "head-2.json", [entry("BM_A", 119), entry("BM_B", 1.1, "us"), entry("BM_New", 7)]
            ),
        ]
        b, h = load(base), load(head)
        assert sorted(b) == ["BM_A", "BM_B", "BM_Gone"], b
        assert b["BM_A"] == [100, 102, 98], "aggregates are skipped, every repetition kept"
        assert b["BM_B"] == [1000.0, 1000.0], "us is scaled to ns"
        rows = {r[0]: r for r in compare(b, h, 15.0)}
        assert rows["BM_A"][1] == 100 and rows["BM_A"][2] == 120, rows["BM_A"]
        assert abs(rows["BM_A"][3] - 20.0) < 1e-9 and rows["BM_A"][4], "20 % slower is flagged"
        assert abs(rows["BM_B"][3] - 10.0) < 1e-6 and not rows["BM_B"][4], "10 % is under +15 %"
        assert rows["BM_New"][1] is None and rows["BM_New"][3] is None and not rows["BM_New"][4]
        assert rows["BM_Gone"][2] is None and not rows["BM_Gone"][4]
        md = markdown(list(rows.values()), 15.0, "T", 3, 3)
        assert "| `BM_A` | 100.0 ns | 120.0 ns | +20.0 % | slower |" in md, md
        assert "| `BM_B` | 1.00 us | 1.10 us | +10.0 % |  |" in md, md
        assert (
            "| `BM_New` | | 7.0 ns | | new |" in md and "| `BM_Gone` | 5.0 ns | | | removed |" in md
        )
        assert "**1 slower than +15 %:** `BM_A`" in md, md
        assert "Nothing slower" in markdown([rows["BM_B"]], 15.0, "T", 1, 1)
        # The command end to end: the warning line, the Markdown file.
        out = dd / "delta.md"
        ns = argparse.Namespace(base=base, head=head, threshold=15.0, markdown=str(out), title="T")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            assert run_compare(ns) == 0
        warn = buf.getvalue().splitlines()
        assert warn == [
            "::warning title=core benchmark slower::BM_A: 100.0 ns -> 120.0 ns "
            "(+20.0 %, threshold +15 %)"
        ], warn
        assert out.read_text().startswith("## T\n"), out.read_text()
        # No base at all (the base predates core/bench): every row is new.
        rows = compare({}, h, 15.0)
        assert all(r[1] is None and not r[4] for r in rows)
    print("bench_compare self-test: OK")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--self-test", action="store_true", help="check this script on fixtures")
    sub = p.add_subparsers(dest="cmd")
    c = sub.add_parser("compare", help="the head against the base")
    c.add_argument("--base", nargs="*", default=[], help="the base build's JSON files")
    c.add_argument("--head", nargs="+", required=True, help="the head build's JSON files")
    c.add_argument(
        "--threshold", type=float, default=15.0, help="flag a slowdown above this %% (15)"
    )
    c.add_argument("--markdown", help="write the table here instead of stdout")
    c.add_argument("--title", default="Core microbenchmarks: this PR against its base")
    args = p.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.cmd == "compare":
        return run_compare(args)
    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
