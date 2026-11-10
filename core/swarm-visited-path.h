// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The bounded stack of nodes an ant (or any agent packet) has visited.
// Derived from swarm-routing/AntHocNet core/include/anthocnet/core/visited_path.h
// @ 78c4640 (MIT OR GPL-2.0-only; there a plain std::vector<AntHop>, bounded by
// Config::maxPathLength in the router) and the path section of
// core/src/ant_message_codec.cpp. Here the cap travels with the path, so no
// caller can grow it past its bound (golden rule 7).
#ifndef SWARM_CORE_VISITED_PATH_H
#define SWARM_CORE_VISITED_PATH_H

#include "swarm-types.h"
#include "swarm-wire.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace swarm::core {

/// One visited node and the time the agent took to reach it from the
/// previous hop (or any per-hop scalar a protocol carries there).
struct Hop {
    NodeAddress node = kInvalidAddress;
    Time time = 0.0;

    bool operator==(const Hop&) const = default;
};

/// A visited path with a fixed capacity. push() refuses a hop once the path
/// is full, so the caller decides what an over-long path means (usually: drop
/// the agent).
class VisitedPath {
public:
    /// Bytes one hop takes on the wire: i32 node + f64 time.
    static constexpr std::size_t kHopWireSize = 4 + 8;
    /// The largest capacity the wire's u16 count can carry.
    static constexpr std::size_t kMaxCapacity = std::numeric_limits<std::uint16_t>::max();

    /// An empty path holding at most `capacity` hops (at least 1, at most
    /// kMaxCapacity).
    explicit VisitedPath(std::size_t capacity)
        : capacity_(std::clamp<std::size_t>(capacity, 1, kMaxCapacity)) {}

    /// Appends a hop; returns false, leaving the path unchanged, when full.
    bool push(NodeAddress node, Time time = 0.0) {
        if (full()) return false;
        hops_.push_back({node, time});
        return true;
    }
    /// Removes the last hop (no-op when empty): a backward agent retracing.
    void pop() {
        if (!hops_.empty()) hops_.pop_back();
    }
    void clear() { hops_.clear(); }

    /// True when `node` is on the path: the loop check before forwarding.
    bool contains(NodeAddress node) const {
        return std::any_of(hops_.begin(), hops_.end(),
                           [node](const Hop& h) { return h.node == node; });
    }

    std::span<const Hop> hops() const { return hops_; }
    std::size_t size() const { return hops_.size(); }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return hops_.empty(); }
    bool full() const { return hops_.size() >= capacity_; }
    /// The last hop; a default Hop (kInvalidAddress) when empty.
    Hop back() const { return hops_.empty() ? Hop{} : hops_.back(); }

    /// Encodes the path: u16 count, then each hop as i32 node + f64 time.
    void write(WireWriter& w) const {
        w.u16(static_cast<std::uint16_t>(hops_.size()));
        for (const Hop& h : hops_) {
            w.i32(h.node);
            w.f64(h.time);
        }
    }

    /// Decodes a path written by write() into `out`, replacing its hops and
    /// keeping its capacity. A count above out.capacity() or a truncated
    /// buffer fails the reader and leaves `out` empty. Returns r.ok().
    static bool read(WireReader& r, VisitedPath& out) {
        out.clear();
        const std::size_t n = r.count(static_cast<std::uint16_t>(out.capacity_), kHopWireSize);
        for (std::size_t i = 0; i < n && r.ok(); ++i) {
            const NodeAddress node = r.i32();
            const Time time = r.f64();
            out.hops_.push_back({node, time});
        }
        if (!r.ok()) out.clear();
        return r.ok();
    }

    bool operator==(const VisitedPath&) const = default;

private:
    std::size_t capacity_;
    std::vector<Hop> hops_;
};

}  // namespace swarm::core

#endif  // SWARM_CORE_VISITED_PATH_H
