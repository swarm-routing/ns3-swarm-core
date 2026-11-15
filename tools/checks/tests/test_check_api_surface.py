# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""Run tools/checks/check-api-surface.py --self-test in this process.

The script's name has hyphens, so it is loaded by path; running it here lets
coverage.py measure it (`coverage run` over this file, reusable-python-ci.yml),
and SonarQube Cloud imports that report (ci.yml, swarm-routing/.github#425).

The script asks for clang++-18 (the clang of ubuntu-24.04, which api-surface.yml
pins so the committed record stays stable). The org's Python job runs on
ubuntu-latest, which may carry only a newer clang; the self-test's fake
headers do not depend on the version, so without clang++-18 and without
CLANG it runs under the newest clang++ on PATH.
"""

import importlib.util
import os
import re
import shutil
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "check-api-surface.py"


def newest_clang() -> str | None:
    found = {}
    for d in os.environ.get("PATH", "").split(os.pathsep):
        for p in Path(d).glob("clang++-*") if Path(d).is_dir() else ():
            m = re.fullmatch(r"clang\+\+-(\d+)", p.name)
            if m and os.access(p, os.X_OK):
                found.setdefault(int(m.group(1)), str(p))
    return found[max(found)] if found else shutil.which("clang++")


if not os.environ.get("CLANG") and not shutil.which("clang++-18"):
    clang = newest_clang()
    if clang:
        print(f"test_check_api_surface: no clang++-18, self-test under {clang}")
        os.environ["CLANG"] = clang

spec = importlib.util.spec_from_file_location("check_api_surface", SCRIPT)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
sys.exit(module.main(["--self-test"]))
