// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#ifndef SWARM_CORE_H
#define SWARM_CORE_H

#include <cstdint>

namespace ns3 {

/// The swarm-core API version. Protocols built against `main` read it to
/// detect a breaking change (golden rule 12); it moves with each one.
/// 1: the seed helper. 2: the core/ primitives and the ns-3 glue
/// (SwarmRoutingBase, DeferredPacketQueue, the link monitors, the helpers).
constexpr uint32_t kSwarmCoreApiVersion = 2;

/// Seeds the simulator's streams the org way: seed 1, run `seed`, so a fixed
/// `--seed=<N>` gives byte-identical output (org invariant 5). Every
/// swarm-routing program calls this once, before creating any random variable.
void SwarmCoreSeed(uint32_t seed);

}  // namespace ns3

#endif  // SWARM_CORE_H
