// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Bounded (source, sequence) duplicate detection. Derived from AntHistoryTracker
// in swarm-routing/AntHocNet core/include/anthocnet/core/ant_history.h and
// core/src/ant_history.cpp @ 78c4640 (MIT OR GPL-2.0-only). AntHocNet's
// GenerationTracker (its multipath acceptance band) stays behind: it is
// AntHocNet's own rule (golden rule 11). Unlike the source, a capacity of 0 is
// not "unbounded": every history here is capped (golden rule 7).
#ifndef SWARM_CORE_DEDUP_HISTORY_H
#define SWARM_CORE_DEDUP_HISTORY_H

#include "swarm-types.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <utility>

namespace swarm::core {

/// Remembers the last `capacity` (source, sequence) pairs seen; the oldest is
/// forgotten first (FIFO).
class DedupHistory {
public:
    /// A history of at most `capacity` entries (at least 1).
    explicit DedupHistory(std::size_t capacity);

    /// Records (src, seq). True when it was new; false for a duplicate, which
    /// the caller usually drops.
    bool record(NodeAddress src, std::uint32_t seq);
    /// Read-only membership test.
    bool seen(NodeAddress src, std::uint32_t seq) const;

    std::size_t size() const { return entries_.size(); }
    std::size_t capacity() const { return capacity_; }
    void clear();

private:
    using Key = std::pair<NodeAddress, std::uint32_t>;

    std::size_t capacity_;
    std::set<Key> entries_;
    std::deque<Key> order_;  // insertion order, for FIFO eviction
};

}  // namespace swarm::core

#endif  // SWARM_CORE_DEDUP_HISTORY_H
