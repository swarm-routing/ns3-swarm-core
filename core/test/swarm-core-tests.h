// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The core test functions, one per primitive (golden rule 13). Each records
// its failures in the Checker; the runners (core/test/run-core-tests.cc and
// the ns-3 suite in test/) report them.
#ifndef SWARM_CORE_TESTS_H
#define SWARM_CORE_TESTS_H

#include "swarm-core-check.h"

#include <span>

namespace swarm::core::test {

void TestWireRoundtrip(Checker& c);
void TestWireFailsClosed(Checker& c);
void TestWireFuzz(Checker& c);
void TestWireWriterCorpus(Checker& c);
void TestTimerPortDefaults(Checker& c);
void TestVisitedPath(Checker& c);
void TestVisitedPathWire(Checker& c);
void TestDedupHistory(Checker& c);
void TestDedupHistoryLifetime(Checker& c);
void TestResetContract(Checker& c);
void TestPheromoneTable(Checker& c);
void TestPheromoneRows(Checker& c);
void TestOnePassEvaporation(Checker& c);
void TestPheromoneTableModel(Checker& c);
void TestEvaporation(Checker& c);
void TestRoulette(Checker& c);
void TestSelectionDeterminism(Checker& c);
void TestSelectionScratch(Checker& c);
void TestRouteDecision(Checker& c);
void TestPortsAndMetric(Checker& c);
void TestEnergyMetric(Checker& c);
void TestObserverCallbacks(Checker& c);
void TestObserverCounters(Checker& c);
void TestObserverNonIntrusive(Checker& c);

/// One named test, for the runners.
struct NamedTest {
    const char* name;
    void (*run)(Checker&);
};

/// Every core test, in a fixed order.
std::span<const NamedTest> AllTests();

}  // namespace swarm::core::test

#endif  // SWARM_CORE_TESTS_H
