# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# swarm-core: the contrib module every swarm-routing protocol links (Phase 2,
# epic #6). core/ holds the simulator-free primitives (#8, MIT OR GPL-2.0-only,
# also buildable alone: core/CMakeLists.txt); model/ and helper/ the ns-3 glue
# (#9: SwarmRoutingBase, DeferredPacketQueue, the link monitors, helpers). ns-3
# generates the umbrella header ns3/swarm-core-module.h from HEADER_FILES, so a
# protocol gets both with `#include "ns3/swarm-core-module.h"`.
#
# The files of core/ are listed once, in core/swarm-core-sources.cmake, which
# the build of core/ alone includes too (#100).
include(${CMAKE_CURRENT_LIST_DIR}/core/swarm-core-sources.cmake)
list(TRANSFORM SWARM_CORE_SOURCES PREPEND core/)
list(TRANSFORM SWARM_CORE_HEADERS PREPEND core/)
list(TRANSFORM SWARM_CORE_TEST_SOURCES PREPEND core/)

build_lib(
  LIBNAME swarm-core
  SOURCE_FILES
    ${SWARM_CORE_SOURCES}
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
    ${SWARM_CORE_HEADERS}
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
    ${SWARM_CORE_TEST_SOURCES}
    test/swarm-core-test-suite.cc
    test/swarm-glue-tests.cc
)
