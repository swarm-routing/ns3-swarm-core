// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet core/src/pheromone_table.cpp and
// core/src/pheromone_engine.cpp (evaporateAll) @ 78c4640 (MIT OR GPL-2.0-only).
#include "swarm-pheromone-table.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace swarm::core {

namespace {

using Entry = PheromoneTable::Entry;

bool neighborLess(const Entry& e, NodeAddress n) {
    return e.neighbor < n;
}

std::vector<Entry>::iterator findEntry(std::vector<Entry>& entries, NodeAddress neighbor) {
    return std::lower_bound(entries.begin(), entries.end(), neighbor, neighborLess);
}

/// The entry for `neighbor` in a row, or nullptr.
const Entry* findIn(std::span<const Entry> entries, NodeAddress neighbor) {
    const auto it = std::lower_bound(entries.begin(), entries.end(), neighbor, neighborLess);
    return it != entries.end() && it->neighbor == neighbor ? &*it : nullptr;
}

}  // namespace

PheromoneTable::PheromoneTable(std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1)) {}

std::vector<PheromoneTable::Row>::iterator PheromoneTable::findRow(NodeAddress dest) {
    auto it = std::lower_bound(rows_.begin(), rows_.end(), dest,
                               [](const Row& r, NodeAddress d) { return r.dest < d; });
    return it != rows_.end() && it->dest == dest ? it : rows_.end();
}

std::vector<PheromoneTable::Row>::const_iterator PheromoneTable::findRow(NodeAddress dest) const {
    auto it = std::lower_bound(rows_.begin(), rows_.end(), dest,
                               [](const Row& r, NodeAddress d) { return r.dest < d; });
    return it != rows_.end() && it->dest == dest ? it : rows_.end();
}

template <class Fn> bool PheromoneTable::update(NodeAddress dest, NodeAddress neighbor, Fn fn) {
    auto rowIt = std::lower_bound(rows_.begin(), rows_.end(), dest,
                                  [](const Row& r, NodeAddress d) { return r.dest < d; });
    if (rowIt != rows_.end() && rowIt->dest == dest) {
        auto e = findEntry(rowIt->entries, neighbor);
        if (e != rowIt->entries.end() && e->neighbor == neighbor) {
            const double value = fn(e->value);
            if (!std::isfinite(value)) return false;
            e->value = value;
            return true;
        }
        const double value = fn(0.0);
        if (!std::isfinite(value) || full()) return false;
        rowIt->entries.insert(e, Entry{neighbor, value});
        ++size_;
        return true;
    }
    const double value = fn(0.0);
    if (!std::isfinite(value) || full()) return false;
    rows_.insert(rowIt, Row{dest, {Entry{neighbor, value}}});
    ++size_;
    return true;
}

bool PheromoneTable::set(NodeAddress dest, NodeAddress neighbor, double value) {
    return update(dest, neighbor, [value](double) { return value; });
}

bool PheromoneTable::add(NodeAddress dest, NodeAddress neighbor, double delta) {
    // old + delta, with old = 0.0 when absent: the arithmetic get() + delta did.
    return update(dest, neighbor, [delta](double old) { return old + delta; });
}

double PheromoneTable::get(NodeAddress dest, NodeAddress neighbor) const {
    const Entry* e = findIn(row(dest), neighbor);
    return e ? e->value : 0.0;
}

bool PheromoneTable::contains(NodeAddress dest, NodeAddress neighbor) const {
    return findIn(row(dest), neighbor) != nullptr;
}

double PheromoneTable::best(NodeAddress dest) const {
    double result = 0.0;
    for (const Entry& e : row(dest))
        result = std::max(result, e.value);
    return result;
}

double PheromoneTable::best(NodeAddress dest, NodeAddress exclude) const {
    double result = 0.0;
    for (const Entry& e : row(dest)) {
        if (e.neighbor != exclude) result = std::max(result, e.value);
    }
    return result;
}

PheromoneTable::Entry PheromoneTable::bestEntry(NodeAddress dest, NodeAddress exclude) const {
    Entry result;
    for (const Entry& e : row(dest)) {
        if (e.neighbor != exclude && e.value > result.value) result = e;
    }
    return result;
}

bool PheromoneTable::erase(NodeAddress dest, NodeAddress neighbor) {
    auto rowIt = findRow(dest);
    if (rowIt == rows_.end()) return false;
    auto e = findEntry(rowIt->entries, neighbor);
    if (e == rowIt->entries.end() || e->neighbor != neighbor) return false;
    rowIt->entries.erase(e);
    --size_;
    if (rowIt->entries.empty()) rows_.erase(rowIt);
    return true;
}

std::size_t PheromoneTable::eraseNeighbor(NodeAddress neighbor) {
    std::size_t removed = 0;
    for (auto& r : rows_) {
        auto e = findEntry(r.entries, neighbor);
        if (e != r.entries.end() && e->neighbor == neighbor) {
            r.entries.erase(e);
            ++removed;
        }
    }
    std::erase_if(rows_, [](const Row& r) { return r.entries.empty(); });
    size_ -= removed;
    return removed;
}

std::size_t PheromoneTable::eraseDestination(NodeAddress dest) {
    auto rowIt = findRow(dest);
    if (rowIt == rows_.end()) return 0;
    const std::size_t removed = rowIt->entries.size();
    rows_.erase(rowIt);
    size_ -= removed;
    return removed;
}

std::span<const PheromoneTable::Entry> PheromoneTable::row(NodeAddress dest) const {
    const auto r = findRow(dest);
    if (r == rows_.end()) return {};
    return r->entries;
}

std::vector<PheromoneTable::Entry> PheromoneTable::entries(NodeAddress dest) const {
    const auto r = row(dest);
    return {r.begin(), r.end()};
}

std::vector<NodeAddress> PheromoneTable::destinations() const {
    std::vector<NodeAddress> out;
    out.reserve(rows_.size());
    for (const Row& r : rows_)
        out.push_back(r.dest);
    return out;
}

std::size_t PheromoneTable::evaporate(double retain, double floor) {
    if (!(retain >= 0.0 && retain <= 1.0)) return 0;  // also rejects NaN
    // One pass: age each entry and drop it when it falls below the floor, the
    // same multiplication and comparison, entry by entry, as the two passes
    // it replaces. Entries move only once one before them has gone.
    std::size_t removed = 0;
    std::size_t keptRows = 0;
    for (std::size_t ri = 0; ri < rows_.size(); ++ri) {
        auto& entries = rows_[ri].entries;
        const std::size_t n = entries.size();
        std::size_t kept = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double value = entries[i].value * retain;
            if (value < floor) continue;
            entries[kept].neighbor = entries[i].neighbor;
            entries[kept].value = value;
            ++kept;
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
