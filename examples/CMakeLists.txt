# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
build_lib_example(
  NAME swarm-core-seed
  SOURCE_FILES swarm-core-seed.cc
  LIBRARIES_TO_LINK ${libswarm-core}
)
