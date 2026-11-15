# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""Run tools/checks/check-api-surface.py --self-test in this process.

The script's name has hyphens, so it is loaded by path; running it here lets
coverage.py measure it (`coverage run` over this file, reusable-python-ci.yml),
and SonarQube Cloud imports that report (ci.yml, swarm-routing/.github#425).
"""

import importlib.util
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "check-api-surface.py"

spec = importlib.util.spec_from_file_location("check_api_surface", SCRIPT)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
sys.exit(module.main(["--self-test"]))
