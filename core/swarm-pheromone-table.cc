// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet core/src/pheromone_table.cpp and
// core/src/pheromone_engine.cpp (evaporateAll) @ 78c4640 (MIT OR GPL-2.0-only).
#include "swarm-pheromone-table.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace swarm::core {

namespace {

constexpr NodeAddress kLowest = std::numeric_limits<NodeAddress>::min();

}  // namespace

PheromoneTable::PheromoneTable(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1)) {}

bool PheromoneTable::set(NodeAddress dest, NodeAddress neighbor, double value) {
    if (!std::isfinite(value)) return false;
    auto it = table_.find({dest, neighbor});
    if (it != table_.end()) {
        it->second = value;
        return true;
    }
    if (full()) return false;
    table_.emplace(Key{dest, neighbor}, value);
    return true;
}

bool PheromoneTable::add(NodeAddress dest, NodeAddress neighbor, double delta) {
    return set(dest, neighbor, get(dest, neighbor) + delta);
}

double PheromoneTable::get(NodeAddress dest, NodeAddress neighbor) const {
    auto it = table_.find({dest, neighbor});
    return it != table_.end() ? it->second : 0.0;
}

bool PheromoneTable::contains(NodeAddress dest, NodeAddress neighbor) const {
    return table_.find({dest, neighbor}) != table_.end();
}

double PheromoneTable::best(NodeAddress dest) const {
    double result = 0.0;
    for (auto it = table_.lower_bound({dest, kLowest});
         it != table_.end() && it->first.first == dest; ++it) {
        result = std::max(result, it->second);
    }
    return result;
}

bool PheromoneTable::erase(NodeAddress dest, NodeAddress neighbor) {
    return table_.erase({dest, neighbor}) > 0;
}

std::size_t PheromoneTable::eraseNeighbor(NodeAddress neighbor) {
    return std::erase_if(table_,
                         [neighbor](const auto& kv) { return kv.first.second == neighbor; });
}

std::size_t PheromoneTable::eraseDestination(NodeAddress dest) {
    return std::erase_if(table_, [dest](const auto& kv) { return kv.first.first == dest; });
}

std::vector<PheromoneTable::Entry> PheromoneTable::entries(NodeAddress dest) const {
    std::vector<Entry> out;
    for (auto it = table_.lower_bound({dest, kLowest});
         it != table_.end() && it->first.first == dest; ++it) {
        out.push_back({it->first.second, it->second});
    }
    return out;
}

std::vector<NodeAddress> PheromoneTable::destinations() const {
    std::vector<NodeAddress> out;
    for (const auto& kv : table_) {
        if (out.empty() || out.back() != kv.first.first) out.push_back(kv.first.first);
    }
    return out;
}

std::size_t PheromoneTable::evaporate(double retain, double floor) {
    if (!(retain >= 0.0 && retain <= 1.0)) return 0;  // also rejects NaN
    for (auto& kv : table_) {
        kv.second *= retain;
    }
    return std::erase_if(table_, [floor](const auto& kv) { return kv.second < floor; });
}

}  // namespace swarm::core
