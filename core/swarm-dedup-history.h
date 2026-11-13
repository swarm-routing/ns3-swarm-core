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
#include <map>
#include <optional>
#include <utility>

namespace swarm::core {

/// Remembers the last `capacity` (source, sequence) pairs seen; the oldest is
/// forgotten first (FIFO).
///
/// Optionally, entries also have a lifetime (#33): an entry recorded at time
/// `t` through the time-aware `record(src, seq, now)` is expired from
/// `t + maxAge` on (an age equal to `maxAge` is expired). An expired entry
/// reads as unseen through `seen(src, seq, now)`, is recorded afresh by
/// `record(src, seq, now)`, and is forgotten by `purge(now)`; until then it
/// still counts against the capacity, so the count cap alone bounds memory
/// (golden rule 7). Time comes in as an argument, never from a clock: core/
/// stays simulator-free (golden rule 10). Without a max age, or through the
/// overloads without `now`, the history behaves exactly as before: entries
/// leave only by the count cap or `clear()`.
class DedupHistory {
public:
    /// A history of at most `capacity` entries (at least 1); `maxAge`, when
    /// given and finite, is the entries' lifetime in seconds (negative reads
    /// as 0: every timed entry is expired at once).
    explicit DedupHistory(std::size_t capacity, std::optional<Time> maxAge = std::nullopt);

    /// Records (src, seq). True when it was new; false for a duplicate, which
    /// the caller usually drops. An entry recorded this way never expires.
    bool record(NodeAddress src, std::uint32_t seq);
    /// Read-only membership test, ignoring the lifetime.
    bool seen(NodeAddress src, std::uint32_t seq) const;

    /// Records (src, seq) at time `now`. True when it was new or its earlier
    /// record had expired at `now` (the entry is then recorded afresh, as the
    /// newest); false for a live duplicate.
    bool record(NodeAddress src, std::uint32_t seq, Time now);
    /// True when (src, seq) is recorded and not expired at `now`.
    bool seen(NodeAddress src, std::uint32_t seq, Time now) const;
    /// Forgets every entry expired at `now`; returns how many. A no-op without
    /// a max age. Call it periodically (ARA: every 1 s) to free the capacity.
    std::size_t purge(Time now);

    std::size_t size() const { return entries_.size(); }
    std::size_t capacity() const { return capacity_; }
    std::optional<Time> maxAge() const { return maxAge_; }
    /// Forgets every entry; capacity and max age are kept (the reset contract,
    /// #43: the history is as freshly constructed).
    void clear();

private:
    using Key = std::pair<NodeAddress, std::uint32_t>;

    bool expired(Time recordedAt, Time now) const;
    bool insert(const Key& key, Time recordedAt);

    std::size_t capacity_;
    std::optional<Time> maxAge_;
    std::map<Key, Time> entries_;  // key -> time recorded (+inf: never expires)
    std::deque<Key> order_;        // insertion order, for FIFO eviction
};

}  // namespace swarm::core

#endif  // SWARM_CORE_DEDUP_HISTORY_H
