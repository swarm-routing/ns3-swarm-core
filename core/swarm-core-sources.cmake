# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# The files of core/, written once (#100). Both builds include this list: core/
# alone (CMakeLists.txt beside it) and the ns-3 module (../CMakeLists.txt,
# which prefixes every name with core/). A new file goes here and nowhere
# else. Names are relative to core/.
set(SWARM_CORE_SOURCES
  swarm-dedup-history.cc
  swarm-observer.cc
  swarm-pheromone-table.cc
  swarm-selection.cc
)
set(SWARM_CORE_HEADERS
  swarm-dedup-history.h
  swarm-link-metric.h
  swarm-observer.h
  swarm-pheromone-table.h
  swarm-ports.h
  swarm-route-decision.h
  swarm-selection.h
  swarm-types.h
  swarm-visited-path.h
  swarm-wire.h
)
# The discrete-event testbench for protocol cores' tests (#15): header-only,
# so neither build compiles anything for it, and it stays out of the ns-3
# module's headers (and so out of ns3/swarm-core-module.h). A consumer that
# links swarm::core includes "testbench/swarm-testbench.h".
set(SWARM_CORE_TESTBENCH_HEADERS
  testbench/swarm-testbench.h
)
# The tests both builds run. core/ alone adds test/run-core-tests.cc (its
# main); the ns-3 module wraps the same functions in its test suite.
set(SWARM_CORE_TEST_SOURCES
  test/all-tests.cc
  test/test-observer.cc
  test/test-selection.cc
  test/test-structures.cc
  test/test-testbench.cc
  test/test-wire.cc
)
