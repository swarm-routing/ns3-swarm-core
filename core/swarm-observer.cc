// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// See swarm-observer.h.
#include "swarm-observer.h"

#include <algorithm>

namespace swarm::core {

const char* findKindName(std::span<const KindName> table, std::uint8_t kind) {
    for (const KindName& k : table) {
        if (k.kind == kind) return k.name;
    }
    return nullptr;
}

void RouterStats::merge(const RouterStats& other) {
    for (const auto& [kind, c] : other.control) {
        ControlCounters& mine = control[kind];
        mine.txPackets += c.txPackets;
        mine.txBytes += c.txBytes;
        mine.txBroadcasts += c.txBroadcasts;
        mine.rxPackets += c.rxPackets;
        mine.rxBytes += c.rxBytes;
    }
    routeAdds += other.routeAdds;
    routeRemoves += other.routeRemoves;
    for (std::size_t i = 0; i < kRouteActionCount; ++i) {
        dataDecisions[i] += other.dataDecisions[i];
    }
    stateEntries += other.stateEntries;
    stateBytes += other.stateBytes;
    peakStateEntries += other.peakStateEntries;
    peakStateBytes += other.peakStateBytes;
    meanStateEntries += other.meanStateEntries;
    meanStateBytes += other.meanStateBytes;
    observedFor = std::max(observedFor, other.observedFor);
}

CountingObserver::CountingObserver(const IClock& clock) : clock_(clock) {
    reset();
}

void CountingObserver::reset() {
    stats_ = RouterStats{};
    start_ = last_ = clock_.now();
    entryArea_ = byteArea_ = 0.0;
}

void CountingObserver::onControlSent(std::uint8_t kind, bool broadcast, std::uint32_t bytes) {
    ControlCounters& c = stats_.control[kind];
    ++c.txPackets;
    c.txBytes += bytes;
    if (broadcast) ++c.txBroadcasts;
}

void CountingObserver::onControlReceived(std::uint8_t kind, std::uint32_t bytes) {
    ControlCounters& c = stats_.control[kind];
    ++c.rxPackets;
    c.rxBytes += bytes;
}

void CountingObserver::onRouteChanged(NodeAddress /*dest*/, NodeAddress /*nextHop*/, bool added) {
    ++(added ? stats_.routeAdds : stats_.routeRemoves);
}

void CountingObserver::onDataDecision(NodeAddress /*dest*/, RouteAction action) {
    const auto i = static_cast<std::size_t>(action);
    if (i < kRouteActionCount) ++stats_.dataDecisions[i];
}

void CountingObserver::accumulate(Time now) {
    if (now > last_) {
        entryArea_ += static_cast<double>(stats_.stateEntries) * (now - last_);
        byteArea_ += static_cast<double>(stats_.stateBytes) * (now - last_);
        last_ = now;
    }
}

void CountingObserver::onStateSize(std::uint32_t entries, std::uint32_t bytes) {
    accumulate(clock_.now());
    stats_.stateEntries = entries;
    stats_.stateBytes = bytes;
    stats_.peakStateEntries = std::max(stats_.peakStateEntries, entries);
    stats_.peakStateBytes = std::max(stats_.peakStateBytes, bytes);
}

RouterStats CountingObserver::stats() const {
    RouterStats s = stats_;
    const Time now = clock_.now();
    const Time last = std::max(last_, now);
    const double entryArea = entryArea_ + static_cast<double>(s.stateEntries) * (last - last_);
    const double byteArea = byteArea_ + static_cast<double>(s.stateBytes) * (last - last_);
    s.observedFor = last - start_;
    if (s.observedFor > 0.0) {
        s.meanStateEntries = entryArea / s.observedFor;
        s.meanStateBytes = byteArea / s.observedFor;
    } else {
        // An empty window: the mean is the footprint itself.
        s.meanStateEntries = s.stateEntries;
        s.meanStateBytes = s.stateBytes;
    }
    return s;
}

}  // namespace swarm::core
