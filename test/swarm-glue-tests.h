// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// The ns-3 glue's test cases (model/, helper/), added to the swarm-core suite.
#ifndef SWARM_GLUE_TESTS_H
#define SWARM_GLUE_TESTS_H

#include "ns3/test.h"

#include <vector>

namespace ns3::swarmtest {

/// One new TestCase per piece of glue; the suite takes ownership.
std::vector<TestCase*> MakeGlueTestCases();

}  // namespace ns3::swarmtest

#endif  // SWARM_GLUE_TESTS_H
