// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "swarm-core.h"

#include "ns3/rng-seed-manager.h"

namespace ns3 {

void SwarmCoreSeed(uint32_t seed) {
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(seed);
}

}  // namespace ns3
