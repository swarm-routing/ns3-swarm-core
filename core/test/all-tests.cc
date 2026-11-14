// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The list both runners walk: core/test/run-core-tests.cc (plain CMake +
// ctest) and test/swarm-core-test-suite.cc (ns-3's test.py).
#include "swarm-core-tests.h"

#include <array>

namespace swarm::core::test {

namespace {

constexpr std::array<NamedTest, 29> kTests{{
    {"wire: roundtrip of every field", TestWireRoundtrip},
    {"wire: the reader fails closed", TestWireFailsClosed},
    {"wire: seeded fuzz (fails or round-trips to itself)", TestWireFuzz},
    {"wire: writers byte-identical to the reference on a corpus", TestWireWriterCorpus},
    {"visited path: bounded", TestVisitedPath},
    {"visited path: wire", TestVisitedPathWire},
    {"dedup history: bounded FIFO", TestDedupHistory},
    {"dedup history: optional entry lifetime", TestDedupHistoryLifetime},
    {"reset contract: clear() is as new, configuration kept", TestResetContract},
    {"pheromone table: bounded", TestPheromoneTable},
    {"pheromone table: rows, forEach, best, add", TestPheromoneRows},
    {"pheromone table: one-pass evaporation", TestOnePassEvaporation},
    {"pheromone table: same as the std::map storage", TestPheromoneTableModel},
    {"evaporation", TestEvaporation},
    {"roulette selection", TestRoulette},
    {"roulette selection: fixed seed, fixed sequence", TestSelectionDeterminism},
    {"roulette selection: caller scratch", TestSelectionScratch},
    {"route decision", TestRouteDecision},
    {"ports and the link-metric seam", TestPortsAndMetric},
    {"timer port: cancellable, additive", TestTimerPortDefaults},
    {"router observer: every callback, kind names", TestObserverCallbacks},
    {"router observer: bounded counters", TestObserverCounters},
    {"router observer: same decisions with and without one", TestObserverNonIntrusive},
    {"testbench: event queue, ordered, cancellable, bounded", TestTestbenchEventQueue},
    {"testbench: topology, links, partition", TestTestbenchTopology},
    {"testbench: ideal MAC, scripted links and loss, the node's ports", TestTestbenchMac},
    {"testbench: the documented adapter example", TestTestbenchExample},
    {"testbench: same seed, identical trace; bounded trace", TestTestbenchDeterminism},
    {"testbench trend: local repair bounded vs re-flood, 6-node line, one break",
     TestTestbenchLocalRepairTrend},
}};

}  // namespace

std::span<const NamedTest> AllTests() {
    return kTests;
}

}  // namespace swarm::core::test
