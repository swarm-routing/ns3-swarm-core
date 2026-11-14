// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A bounded per-node pheromone table: one value per (destination, neighbour).
// Derived from PheromoneTable and PheromoneEngine::evaporateAll in
// swarm-routing/AntHocNet core/include/anthocnet/core/pheromone_table.h,
// core/src/pheromone_table.cpp and core/src/pheromone_engine.cpp @ 78c4640
// (MIT OR GPL-2.0-only); the per-destination evaporate() overload from
// PheromoneTable::ageEntries in the same pheromone_table.h @ 7c3aaf8 (#124).
//
// What stays behind (golden rule 11): AntHocNet's regular/virtual split (a
// protocol that needs two tables holds two), its reinforcement formula and
// hello-diffusion bootstrap. Each protocol writes its own update rule with
// set()/add(); this table only stores, bounds, ages and enumerates.
//
// Storage is one row per destination: a flat vector of (neighbour, value) in
// ascending neighbour order, the rows themselves in ascending destination
// order (#45). Iteration is ordered, never by hash, so whatever a protocol
// derives from it is deterministic (org rule 5); the order is the one the
// earlier std::map on (destination, neighbour) gave.
#ifndef SWARM_CORE_PHEROMONE_TABLE_H
#define SWARM_CORE_PHEROMONE_TABLE_H

#include "swarm-types.h"

#include <cstddef>
#include <span>
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
    /// ARA's. Same refusals as set(). One lookup.
    bool add(NodeAddress dest, NodeAddress neighbor, double delta);
    /// The value, or 0 when there is no entry.
    double get(NodeAddress dest, NodeAddress neighbor) const;
    bool contains(NodeAddress dest, NodeAddress neighbor) const;
    /// The largest value any neighbour holds for `dest`, or 0 when none.
    double best(NodeAddress dest) const;
    /// The same, leaving out `exclude` (e.g. the previous hop).
    double best(NodeAddress dest, NodeAddress exclude) const;
    /// The neighbour holding the largest positive value for `dest`, leaving
    /// out `exclude`; a tie goes to the lowest neighbour. {kInvalidAddress, 0}
    /// when no other neighbour holds a positive value. No allocation.
    Entry bestEntry(NodeAddress dest, NodeAddress exclude = kInvalidAddress) const;

    /// Removes one entry; true when it existed.
    bool erase(NodeAddress dest, NodeAddress neighbor);
    /// Removes every entry through `neighbor` (a lost link); returns how many.
    std::size_t eraseNeighbor(NodeAddress neighbor);
    /// Removes every entry toward `dest`; returns how many.
    std::size_t eraseDestination(NodeAddress dest);
    void clear() {
        rows_.clear();
        size_ = 0;
    }

    /// The entries for `dest`, in ascending neighbour order; empty when none.
    /// No allocation: a view into the table, valid until the next call that
    /// adds or removes an entry (set() or add() on an existing entry keeps
    /// it valid).
    std::span<const Entry> row(NodeAddress dest) const;
    /// Calls f(const Entry&) for each entry of `dest`, in ascending neighbour
    /// order. `f` must not add or remove entries.
    template <class F> void forEach(NodeAddress dest, F&& f) const {
        for (const Entry& e : row(dest))
            f(e);
    }
    /// A copy of row(dest). Prefer row() or forEach() on a hot path.
    std::vector<Entry> entries(NodeAddress dest) const;
    /// Every destination with at least one entry, ascending.
    std::vector<NodeAddress> destinations() const;

    /// Ages every entry: value *= retain, removing the entries left below
    /// `floor`, in one pass. `retain` outside [0, 1] or not finite changes nothing. Use
    /// evaporationFactor() (swarm-selection.h) for a time-proportional
    /// `retain`. Returns how many entries were removed.
    std::size_t evaporate(double retain, double floor);

    /// Ages the table with a retain factor per destination, over a subset of
    /// the entries (#124): AntHocNet's ageing, where only the links to current
    /// neighbours age and a destination may evaporate at its own rate.
    ///
    /// - `retainFor(dest) -> double` is called exactly once per row, in
    ///   ascending destination order, rows whose entries all stay unaged
    ///   included (a caller's per-destination `pow` runs once per row);
    /// - an entry for which `ages(dest, neighbor) -> bool` is false, or whose
    ///   value is <= 0, is kept unchanged (not aged, not removed);
    /// - any other entry becomes value * retain and is removed when below
    ///   `floor`: the same multiplication and comparison as evaporate();
    /// - a row's `retain` outside [0, 1] or not finite leaves that row
    ///   unchanged; a row left empty is removed.
    ///
    /// Neither callback may add or remove entries. Returns how many entries
    /// were removed. evaporate(retain, floor) is unchanged by this overload.
    template <class RetainFor, class Ages>
    std::size_t evaporate(RetainFor&& retainFor, double floor, Ages&& ages);

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    bool full() const { return size_ >= capacity_; }

private:
    /// One destination's entries, ascending by neighbour, never empty.
    struct Row {
        NodeAddress dest = kInvalidAddress;
        std::vector<Entry> entries;
    };

    std::vector<Row>::iterator findRow(NodeAddress dest);
    std::vector<Row>::const_iterator findRow(NodeAddress dest) const;
    /// Writes `value` at (dest, neighbor), creating the entry unless the
    /// table is full; `fn(old)` gives the value from the old one (0 when
    /// absent). One lookup; refuses a non-finite result.
    template <class Fn> bool update(NodeAddress dest, NodeAddress neighbor, Fn fn);

    std::size_t capacity_;
    std::size_t size_ = 0;
    std::vector<Row> rows_;  // ascending by destination
};

template <class RetainFor, class Ages>
std::size_t PheromoneTable::evaporate(RetainFor&& retainFor, double floor, Ages&& ages) {
    // The single pass of evaporate(retain, floor), with the per-row factor
    // and the two filters of AntHocNet's ageEntries.
    std::size_t removed = 0;
    std::size_t keptRows = 0;
    for (std::size_t ri = 0; ri < rows_.size(); ++ri) {
        const NodeAddress dest = rows_[ri].dest;
        const double retain = retainFor(dest);
        auto& entries = rows_[ri].entries;
        const std::size_t n = entries.size();
        std::size_t kept = 0;
        if (retain >= 0.0 && retain <= 1.0) {  // also rejects NaN
            for (std::size_t i = 0; i < n; ++i) {
                double value = entries[i].value;
                if (!(value <= 0.0) && ages(dest, entries[i].neighbor)) {
                    value *= retain;
                    if (value < floor) continue;
                }
                entries[kept].neighbor = entries[i].neighbor;
                entries[kept].value = value;
                ++kept;
            }
        } else {
            kept = n;
        }
        removed += n - kept;
        entries.resize(kept);
        if (kept == 0) continue;
        if (keptRows != ri) rows_[keptRows] = std::move(rows_[ri]);
        ++keptRows;
    }
    rows_.resize(keptRows);
    size_ -= removed;
    return removed;
}

}  // namespace swarm::core

#endif  // SWARM_CORE_PHEROMONE_TABLE_H
