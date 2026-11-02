// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Prints the first draws of a seeded stream. CI's determinism gate runs it
// twice with --seed=1 (output must be byte-identical) and once with --seed=2
// (output must differ), until the module's own primitives give it more to run.
#include "ns3/command-line.h"
#include "ns3/random-variable-stream.h"
#include "ns3/swarm-core-module.h"

#include <iomanip>
#include <iostream>

using namespace ns3;

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
        std::cout << "##DRAW## seed=" << seed << " i=" << i << " u=" << u->GetValue() << "\n";
    }
    return 0;
}
