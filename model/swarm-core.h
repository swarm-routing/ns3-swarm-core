// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#ifndef SWARM_CORE_H
#define SWARM_CORE_H

#include <cstdint>

namespace ns3 {

/// The swarm-core API version, for a consumer's compile-time check
/// (`static_assert(ns3::kSwarmCoreApiVersion == 2)`). Before 1.0 it moved with
/// each breaking change: 1, the seed helper; 2, the core/ primitives and the
/// ns-3 glue (SwarmRoutingBase, DeferredPacketQueue, the link monitors, the
/// helpers). v1.0.0 froze it at 2, the API every 0.2-0.4 release shipped
/// additively: it holds for all of 1.x and moves by one per major only
/// (docs/api-policy.md "kSwarmCoreApiVersion").
// The rule (maintainer, 2026-11-15): it increases when the API changes in a
// breaking way, and is kept otherwise. Nothing broke since 2: 0.3 and 0.4
// were additive, and marking FuzzyMetric and IslLinkMonitor experimental
// removed nothing.
constexpr uint32_t kSwarmCoreApiVersion = 2;

/// Seeds the simulator's streams the org way: seed 1, run `seed`, so a fixed
/// `--seed=<N>` gives byte-identical output (org invariant 5). Every
/// swarm-routing program calls this once, before creating any random variable.
void SwarmCoreSeed(uint32_t seed);

}  // namespace ns3

#endif  // SWARM_CORE_H
