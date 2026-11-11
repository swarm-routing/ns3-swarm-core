// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Stochastic (roulette) next-hop selection and time-proportional evaporation.
// Derived from PheromoneTable::nextNeighborNode / nextHopDistribution and
// PheromoneEngine::evaporateAll in swarm-routing/AntHocNet
// core/src/pheromone_table.cpp and core/src/pheromone_engine.cpp @ 78c4640
// (MIT OR GPL-2.0-only).
//
// Every draw goes through the injected IRng, exactly one uniform() per
// selection, so a fixed seed gives a byte-identical selection sequence (org
// rule 5). Unlike AntHocNet, the weights are computed as (value / best)^beta,
// which gives the same distribution as value^beta but cannot overflow for a
// large beta.
#ifndef SWARM_CORE_SELECTION_H
#define SWARM_CORE_SELECTION_H

#include "swarm-pheromone-table.h"
#include "swarm-ports.h"
#include "swarm-types.h"

#include <span>
#include <vector>

namespace swarm::core {

/// A candidate and its (unnormalised) selection weight.
struct WeightedHop {
    NodeAddress node = kInvalidAddress;
    double weight = 0.0;

    bool operator==(const WeightedHop&) const = default;
};

/// Draws one candidate with probability weight / sum(weights). Weights that
/// are not finite or not positive never win. Draws exactly one rng.uniform()
/// when at least one weight is positive; returns kInvalidAddress without
/// drawing otherwise.
NodeAddress rouletteSelect(std::span<const WeightedHop> candidates, IRng& rng);

/// The selection weights of the neighbours holding pheromone for `dest`,
/// (value / best)^beta, in ascending neighbour order; entries with no positive
/// value are left out. `exclude` (e.g. the previous hop) is left out too,
/// unless it is the only candidate: a packet goes back the way it came rather
/// than nowhere.
std::vector<WeightedHop> selectionWeights(const PheromoneTable& table, NodeAddress dest,
                                          double beta, NodeAddress exclude = kInvalidAddress);
/// The same weights written into `out` (cleared first), so a caller that
/// keeps `out` across calls allocates only while it grows. Returns a view of
/// `out`.
std::span<const WeightedHop> selectionWeights(const PheromoneTable& table, NodeAddress dest,
                                              double beta, std::vector<WeightedHop>& out,
                                              NodeAddress exclude = kInvalidAddress);

/// selectionWeights() normalised to probabilities that sum to 1 (empty when
/// there is no route). Read-only: it draws nothing, for inspection and tests.
std::vector<WeightedHop> selectionDistribution(const PheromoneTable& table, NodeAddress dest,
                                               double beta, NodeAddress exclude = kInvalidAddress);

/// Roulette over selectionWeights(): the next hop toward `dest`, or
/// kInvalidAddress when the table holds no route (and then nothing is drawn).
NodeAddress selectNextHop(const PheromoneTable& table, NodeAddress dest, double beta, IRng& rng,
                          NodeAddress exclude = kInvalidAddress);
/// The same selection, the same draws, with the weights in the caller's
/// `scratch` (overwritten): no allocation once `scratch` has grown to the
/// widest row.
NodeAddress selectNextHop(const PheromoneTable& table, NodeAddress dest, double beta, IRng& rng,
                          std::vector<WeightedHop>& scratch, NodeAddress exclude = kInvalidAddress);

/// The retain factor for `elapsed` seconds when a value keeps `retainPerInterval`
/// of itself every `interval` seconds: retainPerInterval^(elapsed / interval).
/// 1 (no ageing) when elapsed or interval is not positive, or retainPerInterval
/// is outside [0, 1].
double evaporationFactor(double retainPerInterval, Time elapsed, Time interval);

}  // namespace swarm::core

#endif  // SWARM_CORE_SELECTION_H
