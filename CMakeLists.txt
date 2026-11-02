# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# swarm-core: the contrib module every swarm-routing protocol links (Phase 2,
# epic #6). This is the skeleton (#7); the simulator-free primitives arrive in
# core/ with #8, the ns-3 glue in model/ with #9. ns-3 generates the umbrella
# header ns3/swarm-core-module.h from HEADER_FILES.
build_lib(
  LIBNAME swarm-core
  SOURCE_FILES model/swarm-core.cc
  HEADER_FILES model/swarm-core.h
  LIBRARIES_TO_LINK ${libcore} ${libnetwork} ${libinternet}
  TEST_SOURCES test/swarm-core-test-suite.cc
)
