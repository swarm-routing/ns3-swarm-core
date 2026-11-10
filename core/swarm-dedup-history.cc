// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet core/src/ant_history.cpp @ 78c4640
// (MIT OR GPL-2.0-only).
#include "swarm-dedup-history.h"

#include <algorithm>

namespace swarm::core {

DedupHistory::DedupHistory(std::size_t capacity) : capacity_(std::max<std::size_t>(capacity, 1)) {}

bool DedupHistory::record(NodeAddress src, std::uint32_t seq) {
    const Key key{src, seq};
    if (!entries_.insert(key).second) return false;
    order_.push_back(key);
    while (order_.size() > capacity_) {
        entries_.erase(order_.front());
        order_.pop_front();
    }
    return true;
}

bool DedupHistory::seen(NodeAddress src, std::uint32_t seq) const {
    return entries_.find({src, seq}) != entries_.end();
}

void DedupHistory::clear() {
    entries_.clear();
    order_.clear();
}

}  // namespace swarm::core
