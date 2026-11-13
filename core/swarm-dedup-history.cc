// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet core/src/ant_history.cpp @ 78c4640
// (MIT OR GPL-2.0-only).
#include "swarm-dedup-history.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace swarm::core {

namespace {
constexpr Time kNever = std::numeric_limits<Time>::infinity();
}  // namespace

DedupHistory::DedupHistory(std::size_t capacity, std::optional<Time> maxAge)
    : capacity_(std::max<std::size_t>(capacity, 1)) {
    if (maxAge && std::isfinite(*maxAge)) maxAge_ = std::max<Time>(*maxAge, 0.0);
}

bool DedupHistory::expired(Time recordedAt, Time now) const {
    return maxAge_ && recordedAt != kNever && now - recordedAt >= *maxAge_;
}

bool DedupHistory::insert(const Key& key, Time recordedAt) {
    if (!entries_.emplace(key, recordedAt).second) return false;
    order_.push_back(key);
    while (order_.size() > capacity_) {
        entries_.erase(order_.front());
        order_.pop_front();
    }
    return true;
}

bool DedupHistory::record(NodeAddress src, std::uint32_t seq) {
    return insert({src, seq}, kNever);
}

bool DedupHistory::seen(NodeAddress src, std::uint32_t seq) const {
    return entries_.find({src, seq}) != entries_.end();
}

bool DedupHistory::record(NodeAddress src, std::uint32_t seq, Time now) {
    const Key key{src, seq};
    const auto it = entries_.find(key);
    if (it != entries_.end()) {
        if (!expired(it->second, now)) return false;
        // Expired but not yet purged: forget the old record, then record afresh.
        entries_.erase(it);
        order_.erase(std::find(order_.begin(), order_.end(), key));
    }
    return insert(key, now);
}

bool DedupHistory::seen(NodeAddress src, std::uint32_t seq, Time now) const {
    const auto it = entries_.find({src, seq});
    return it != entries_.end() && !expired(it->second, now);
}

std::size_t DedupHistory::purge(Time now) {
    if (!maxAge_) return 0;
    const std::size_t removed = std::erase_if(order_, [&](const Key& key) {
        const auto it = entries_.find(key);
        if (!expired(it->second, now)) return false;
        entries_.erase(it);
        return true;
    });
    return removed;
}

void DedupHistory::clear() {
    entries_.clear();
    order_.clear();
}

}  // namespace swarm::core
