// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Prints the first draws of a seeded stream. CI's determinism gate runs it
// twice with --seed=1 (output must be byte-identical) and once with --seed=2
// (output must differ). The seed itself is not printed: a label that changes
// with the seed would make "seed 2 differs" pass even if the seed were ignored.
// It also prints a roulette next-hop sequence from the core/ primitives, drawn
// through an IRng port over ns-3's seeded stream: what a protocol's core does,
// so the gate covers the primitives' determinism too (org rule 5).
#include "ns3/command-line.h"
#include "ns3/random-variable-stream.h"
#include "ns3/swarm-core-module.h"

#include <iomanip>
#include <iostream>

using namespace ns3;

namespace {

/// swarm::core::IRng over an ns-3 UniformRandomVariable.
class StreamRng : public swarm::core::IRng {
public:
    explicit StreamRng(Ptr<UniformRandomVariable> u) : m_u(u) {}
    double uniform() override { return m_u->GetValue(); }
    int uniformInt(int n) override { return static_cast<int>(m_u->GetInteger(0, n - 1)); }

private:
    Ptr<UniformRandomVariable> m_u;
};

}  // namespace

int main(int argc, char* argv[]) {
    uint32_t seed = 1;
    uint32_t draws = 5;
    CommandLine cmd(__FILE__);
    cmd.AddValue("seed", "run number of the seeded stream", seed);
    cmd.AddValue("draws", "values to print", draws);
    cmd.Parse(argc, argv);

    SwarmCoreSeed(seed);
    Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable>();
    std::cout << std::setprecision(17);
    for (uint32_t i = 0; i < draws; ++i) {
        std::cout << "##DRAW## i=" << i << " u=" << u->GetValue() << "\n";
    }

    swarm::core::PheromoneTable table(16);
    table.set(9, 1, 0.2);
    table.set(9, 2, 0.5);
    table.set(9, 3, 0.3);
    StreamRng rng(u);
    for (uint32_t i = 0; i < draws; ++i) {
        std::cout << "##PICK## i=" << i
                  << " next=" << swarm::core::selectNextHop(table, 9, 2.0, rng) << "\n";
    }
    return 0;
}
