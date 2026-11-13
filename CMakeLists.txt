# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# swarm-core: the contrib module every swarm-routing protocol links (Phase 2,
# epic #6). core/ holds the simulator-free primitives (#8, MIT OR GPL-2.0-only,
# also buildable alone: core/CMakeLists.txt); model/ and helper/ the ns-3 glue
# (#9: SwarmRoutingBase, DeferredPacketQueue, the link monitors, helpers). ns-3
# generates the umbrella header ns3/swarm-core-module.h from HEADER_FILES, so a
# protocol gets both with `#include "ns3/swarm-core-module.h"`.
build_lib(
  LIBNAME swarm-core
  SOURCE_FILES
    core/swarm-dedup-history.cc
    core/swarm-observer.cc
    core/swarm-pheromone-table.cc
    core/swarm-selection.cc
    helper/swarm-lifecycle-helper.cc
    helper/swarm-routing-helper.cc
    model/swarm-core.cc
    model/swarm-deferred-packet-queue.cc
    model/swarm-energy-metric-hook.cc
    model/swarm-isl-link-monitor.cc
    model/swarm-ns3-ports.cc
    model/swarm-routing-base.cc
    model/swarm-wifi-link-monitor.cc
  HEADER_FILES
    core/swarm-dedup-history.h
    core/swarm-link-metric.h
    core/swarm-observer.h
    core/swarm-pheromone-table.h
    core/swarm-ports.h
    core/swarm-route-decision.h
    core/swarm-selection.h
    core/swarm-types.h
    core/swarm-visited-path.h
    core/swarm-wire.h
    helper/swarm-lifecycle-helper.h
    helper/swarm-routing-helper.h
    model/swarm-core.h
    model/swarm-deferred-packet-queue.h
    model/swarm-energy-metric-hook.h
    model/swarm-isl-link-monitor.h
    model/swarm-ns3-ports.h
    model/swarm-routing-base.h
    model/swarm-wifi-link-monitor.h
    model/swarm-wire-buffer.h
  LIBRARIES_TO_LINK ${libcore} ${libnetwork} ${libinternet} ${libwifi} ${libpoint-to-point}
                    ${libenergy} ${libmobility}
  TEST_SOURCES
    core/test/all-tests.cc
    core/test/test-observer.cc
    core/test/test-selection.cc
    core/test/test-structures.cc
    core/test/test-wire.cc
    test/swarm-core-test-suite.cc
    test/swarm-glue-tests.cc
)
