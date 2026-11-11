// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet core/src/pheromone_table.cpp
// (nextNeighborNode, nextHopDistribution) and core/src/pheromone_engine.cpp
// (evaporateAll) @ 78c4640 (MIT OR GPL-2.0-only).
#include "swarm-selection.h"

#include <cmath>

namespace swarm::core {

namespace {

bool usable(double w) {
    return std::isfinite(w) && w > 0.0;
}

void weightsExcluding(std::span<const PheromoneTable::Entry> entries, double beta,
                      NodeAddress exclude, std::vector<WeightedHop>& out) {
    out.clear();
    double best = 0.0;
    for (const auto& e : entries) {
        if (e.neighbor != exclude && e.value > best) best = e.value;
    }
    if (best <= 0.0) return;
    for (const auto& e : entries) {
        if (e.neighbor == exclude || e.value <= 0.0) continue;
        out.push_back({e.neighbor, std::pow(e.value / best, beta)});
    }
}

}  // namespace

NodeAddress rouletteSelect(std::span<const WeightedHop> candidates, IRng& rng) {
    double sum = 0.0;
    for (const auto& c : candidates) {
        if (usable(c.weight)) sum += c.weight;
    }
    if (!usable(sum)) return kInvalidAddress;

    double r = rng.uniform() * sum;
    NodeAddress chosen = kInvalidAddress;
    for (const auto& c : candidates) {
        if (!usable(c.weight)) continue;
        chosen = c.node;
        r -= c.weight;
        if (r < 0.0) break;
    }
    // Rounding can leave r a hair above 0 after the last candidate; it then
    // keeps the last usable one, never an unusable one.
    return chosen;
}

std::span<const WeightedHop> selectionWeights(const PheromoneTable& table, NodeAddress dest,
                                              double beta, std::vector<WeightedHop>& out,
                                              NodeAddress exclude) {
    const auto entries = table.row(dest);
    weightsExcluding(entries, beta, exclude, out);
    if (out.empty() && exclude != kInvalidAddress) {
        weightsExcluding(entries, beta, kInvalidAddress, out);
    }
    return out;
}

std::vector<WeightedHop> selectionWeights(const PheromoneTable& table, NodeAddress dest,
                                          double beta, NodeAddress exclude) {
    std::vector<WeightedHop> out;
    selectionWeights(table, dest, beta, out, exclude);
    return out;
}

std::vector<WeightedHop> selectionDistribution(const PheromoneTable& table, NodeAddress dest,
                                               double beta, NodeAddress exclude) {
    auto weights = selectionWeights(table, dest, beta, exclude);
    double sum = 0.0;
    for (const auto& w : weights) {
        if (usable(w.weight)) sum += w.weight;
    }
    std::vector<WeightedHop> out;
    if (!usable(sum)) return out;
    for (const auto& w : weights) {
        if (usable(w.weight)) out.push_back({w.node, w.weight / sum});
    }
    return out;
}

NodeAddress selectNextHop(const PheromoneTable& table, NodeAddress dest, double beta, IRng& rng,
                          NodeAddress exclude) {
    std::vector<WeightedHop> scratch;
    return selectNextHop(table, dest, beta, rng, scratch, exclude);
}

NodeAddress selectNextHop(const PheromoneTable& table, NodeAddress dest, double beta, IRng& rng,
                          std::vector<WeightedHop>& scratch, NodeAddress exclude) {
    return rouletteSelect(selectionWeights(table, dest, beta, scratch, exclude), rng);
}

double evaporationFactor(double retainPerInterval, Time elapsed, Time interval) {
    if (!(elapsed > 0.0) || !(interval > 0.0) ||
        !(retainPerInterval >= 0.0 && retainPerInterval <= 1.0)) {
        return 1.0;
    }
    return std::pow(retainPerInterval, elapsed / interval);
}

}  // namespace swarm::core
