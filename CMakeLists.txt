# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# swarm-core: the contrib module every swarm-routing protocol links (Phase 2,
# epic #6). core/ holds the simulator-free primitives (#8, MIT OR GPL-2.0-only,
# also buildable alone: core/CMakeLists.txt); model/ the ns-3 glue (#9). ns-3
# generates the umbrella header ns3/swarm-core-module.h from HEADER_FILES, so a
# protocol gets both with `#include "ns3/swarm-core-module.h"`.
build_lib(
  LIBNAME swarm-core
  SOURCE_FILES
    core/swarm-dedup-history.cc
    core/swarm-pheromone-table.cc
    core/swarm-selection.cc
    model/swarm-core.cc
  HEADER_FILES
    core/swarm-dedup-history.h
    core/swarm-link-metric.h
    core/swarm-pheromone-table.h
    core/swarm-ports.h
    core/swarm-route-decision.h
    core/swarm-selection.h
    core/swarm-types.h
    core/swarm-visited-path.h
    core/swarm-wire.h
    model/swarm-core.h
  LIBRARIES_TO_LINK ${libcore} ${libnetwork} ${libinternet}
  TEST_SOURCES
    core/test/all-tests.cc
    core/test/test-selection.cc
    core/test/test-structures.cc
    core/test/test-wire.cc
    test/swarm-core-test-suite.cc
)
