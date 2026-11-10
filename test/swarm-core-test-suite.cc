// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Needs ns-3.42+ (scoped TestCase::Duration); older releases are on demand only.
//
// The core/ tests (core/test/, one function per primitive) run here as one
// TestCase each, so test.py covers them on the debug and sanitized legs; the
// same functions run without ns-3 under core/CMakeLists.txt.
#include "../core/test/swarm-core-tests.h"

#include "ns3/swarm-core-module.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/test.h"

#include <string>

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

/// One core/ test function, its failures reported through ns-3.
class SwarmCorePrimitiveTestCase : public TestCase {
public:
    explicit SwarmCorePrimitiveTestCase(const swarm::core::test::NamedTest& t)
        : TestCase(std::string("core: ") + t.name), m_test(t) {}

private:
    void DoRun() override {
        swarm::core::test::Checker c;
        m_test.run(c);
        // Each failure prints as "got <file:line: check>, expected ''".
        for (const auto& f : c.failures()) {
            NS_TEST_EXPECT_MSG_EQ(f, std::string(), "core check failed");
        }
    }

    swarm::core::test::NamedTest m_test;
};

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
        for (const auto& t : swarm::core::test::AllTests()) {
            AddTestCase(new SwarmCorePrimitiveTestCase(t), TestCase::Duration::QUICK);
        }
    }
};

static SwarmCoreTestSuite g_swarmCoreTestSuite;
