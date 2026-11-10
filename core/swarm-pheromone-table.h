// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A bounded per-node pheromone table: one value per (destination, neighbour).
// Derived from PheromoneTable and PheromoneEngine::evaporateAll in
// swarm-routing/AntHocNet core/include/anthocnet/core/pheromone_table.h,
// core/src/pheromone_table.cpp and core/src/pheromone_engine.cpp @ 78c4640
// (MIT OR GPL-2.0-only).
//
// What stays behind (golden rule 11): AntHocNet's regular/virtual split (a
// protocol that needs two tables holds two), its reinforcement formula and
// hello-diffusion bootstrap. Each protocol writes its own update rule with
// set()/add(); this table only stores, bounds, ages and enumerates.
//
// Iteration is ordered (std::map on (destination, neighbour)), never by hash,
// so whatever a protocol derives from it is deterministic (org rule 5).
#ifndef SWARM_CORE_PHEROMONE_TABLE_H
#define SWARM_CORE_PHEROMONE_TABLE_H

#include "swarm-types.h"

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace swarm::core {

class PheromoneTable {
public:
    /// One neighbour's pheromone toward a destination.
    struct Entry {
        NodeAddress neighbor = kInvalidAddress;
        double value = 0.0;

        bool operator==(const Entry&) const = default;
    };

    /// A table of at most `capacity` (destination, neighbour) entries (at
    /// least 1). The table grows with destinations x neighbours, so it is
    /// capped (golden rule 7).
    explicit PheromoneTable(std::size_t capacity);

    /// Sets the value. Returns false, changing nothing, when `value` is not
    /// finite, or when the entry is new and the table is full.
    bool set(NodeAddress dest, NodeAddress neighbor, double value);
    /// Adds `delta` to the value (0 when absent): an additive deposit, as
    /// ARA's. Same refusals as set().
    bool add(NodeAddress dest, NodeAddress neighbor, double delta);
    /// The value, or 0 when there is no entry.
    double get(NodeAddress dest, NodeAddress neighbor) const;
    bool contains(NodeAddress dest, NodeAddress neighbor) const;
    /// The largest value any neighbour holds for `dest`, or 0 when none.
    double best(NodeAddress dest) const;

    /// Removes one entry; true when it existed.
    bool erase(NodeAddress dest, NodeAddress neighbor);
    /// Removes every entry through `neighbor` (a lost link); returns how many.
    std::size_t eraseNeighbor(NodeAddress neighbor);
    /// Removes every entry toward `dest`; returns how many.
    std::size_t eraseDestination(NodeAddress dest);
    void clear() { table_.clear(); }

    /// The neighbours holding an entry for `dest`, in ascending neighbour order.
    std::vector<Entry> entries(NodeAddress dest) const;
    /// Every destination with at least one entry, ascending.
    std::vector<NodeAddress> destinations() const;

    /// Ages every entry: value *= retain, then removes the entries left below
    /// `floor`. `retain` outside [0, 1] or not finite changes nothing. Use
    /// evaporationFactor() (swarm-selection.h) for a time-proportional
    /// `retain`. Returns how many entries were removed.
    std::size_t evaporate(double retain, double floor);

    std::size_t size() const { return table_.size(); }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return table_.empty(); }
    bool full() const { return table_.size() >= capacity_; }

private:
    using Key = std::pair<NodeAddress, NodeAddress>;  // (dest, neighbour)

    std::size_t capacity_;
    std::map<Key, double> table_;
};

}  // namespace swarm::core

#endif  // SWARM_CORE_PHEROMONE_TABLE_H
