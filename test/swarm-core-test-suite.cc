// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Needs ns-3.42+ (scoped TestCase::Duration); older releases are on demand only.
#include "ns3/swarm-core-module.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/test.h"

using namespace ns3;

namespace {

// ns-3 hands each new random variable the next stream number, so two
// variables only draw alike when their stream is pinned (as AssignStreams does).
double FirstDraw(uint32_t seed) {
    SwarmCoreSeed(seed);
    Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable>();
    u->SetStream(1);
    return u->GetValue();
}

}  // namespace

class SwarmCoreSeedTestCase : public TestCase {
public:
    SwarmCoreSeedTestCase() : TestCase("SwarmCoreSeed: same seed same stream, other seed other") {}

private:
    void DoRun() override {
        NS_TEST_ASSERT_MSG_EQ(kSwarmCoreApiVersion, 1u, "API version");
        const double a = FirstDraw(7);
        NS_TEST_ASSERT_MSG_EQ(RngSeedManager::GetSeed(), 1u, "seed is always 1");
        NS_TEST_ASSERT_MSG_EQ(RngSeedManager::GetRun(), 7u, "the run is the seed");
        NS_TEST_ASSERT_MSG_EQ(FirstDraw(7), a, "same seed, same first draw");
        NS_TEST_ASSERT_MSG_NE(FirstDraw(8), a, "another seed, another first draw");
    }
};

class SwarmCoreTestSuite : public TestSuite {
public:
    SwarmCoreTestSuite() : TestSuite("swarm-core", Type::UNIT) {
        AddTestCase(new SwarmCoreSeedTestCase, TestCase::Duration::QUICK);
    }
};

static SwarmCoreTestSuite g_swarmCoreTestSuite;
