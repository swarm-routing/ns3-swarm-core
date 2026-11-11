// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// IRouterObserver: the events a protocol's core reports, so every protocol is
// observed the same way (#42). Derived from IRouterObserver in
// swarm-routing/AntHocNet core/include/anthocnet/core/ports.h @ 78c4640
// (MIT OR GPL-2.0-only; onAntSent, onAntReceived, onRouteChanged), widened by
// bytes, data decisions and the routing-state footprint, with nothing
// protocol-specific: a control message is a `kind`, the protocol's own wire
// type byte, and the protocol names its kinds in a static KindName table.
//
// An observer only reads. It never makes a routing decision, does I/O or
// draws randomness, so the same seed gives the same run with or without one
// (org rule 5). It is optional: a core holds an `IRouterObserver*` and a null
// pointer means none, at the cost of one pointer check.
//
// CountingObserver turns the events into bounded counters (org rule 7): one
// entry per kind (at most 256, the width of the byte), one per RouteAction,
// and running sums for the state footprint; it records no history. Counters
// iterate in kind order (std::map), never by hash (org rule 5).
#ifndef SWARM_CORE_OBSERVER_H
#define SWARM_CORE_OBSERVER_H

#include "swarm-ports.h"
#include "swarm-route-decision.h"
#include "swarm-types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

namespace swarm::core {

/// How many RouteAction values there are (DiscardPending is the last).
inline constexpr std::size_t kRouteActionCount =
    static_cast<std::size_t>(RouteAction::DiscardPending) + 1;

/// Optional observer a protocol's core notifies of routing events. Every
/// method defaults to a no-op, so an observer overrides only what it reads.
class IRouterObserver {
public:
    virtual ~IRouterObserver() = default;
    /// This node put a control message of `kind` on the medium (originated or
    /// forwarded), `bytes` long as the protocol encodes it.
    virtual void onControlSent(std::uint8_t /*kind*/, bool /*broadcast*/, std::uint32_t /*bytes*/) {
    }
    /// This node processed a received control message of `kind`.
    virtual void onControlReceived(std::uint8_t /*kind*/, std::uint32_t /*bytes*/) {}
    /// A route entry toward `dest` through `nextHop` was added (true) or
    /// removed (false).
    virtual void onRouteChanged(NodeAddress /*dest*/, NodeAddress /*nextHop*/, bool /*added*/) {}
    /// What the core decided for a data packet to `dest`: Unicast (forwarded),
    /// Queue, Drop (no route, loop, TTL) or Deliver.
    virtual void onDataDecision(NodeAddress /*dest*/, RouteAction /*action*/) {}
    /// The protocol's routing-state footprint: `entries` table entries taking
    /// `bytes`. Reported when it changes, or on demand.
    virtual void onStateSize(std::uint32_t /*entries*/, std::uint32_t /*bytes*/) {}
};

/// One entry of a protocol's kind-name table: its wire type byte and the name
/// readers print ("FANT", not "3"). A protocol keeps a static array of these.
struct KindName {
    std::uint8_t kind = 0;
    const char* name = nullptr;
};

/// The name of `kind` in `table`, or nullptr when the table does not name it.
const char* findKindName(std::span<const KindName> table, std::uint8_t kind);

/// The control traffic of one kind.
struct ControlCounters {
    std::uint64_t txPackets = 0;
    std::uint64_t txBytes = 0;
    std::uint64_t txBroadcasts = 0;  ///< of txPackets
    std::uint64_t rxPackets = 0;
    std::uint64_t rxBytes = 0;

    bool operator==(const ControlCounters&) const = default;
};

/// What a CountingObserver counted. For one node; merge() sums nodes.
struct RouterStats {
    /// By kind, ascending: at most 256 entries.
    std::map<std::uint8_t, ControlCounters> control;
    std::uint64_t routeAdds = 0;
    std::uint64_t routeRemoves = 0;
    /// Data decisions, indexed by RouteAction.
    std::array<std::uint64_t, kRouteActionCount> dataDecisions{};
    /// The footprint last reported, its peak, and its time average over the
    /// observed window (0 before the first report).
    std::uint32_t stateEntries = 0;
    std::uint32_t stateBytes = 0;
    std::uint32_t peakStateEntries = 0;
    std::uint32_t peakStateBytes = 0;
    double meanStateEntries = 0.0;
    double meanStateBytes = 0.0;
    /// Length of the observed window (seconds).
    Time observedFor = 0.0;

    std::uint64_t decisions(RouteAction a) const {
        return dataDecisions[static_cast<std::size_t>(a)];
    }
    /// Adds `other` in: counts and footprints sum (peaks and means become the
    /// network's sum of per-node values), the window is the longer one.
    void merge(const RouterStats& other);

    bool operator==(const RouterStats&) const = default;
};

/// An observer that counts, bounded, against `clock` for the time averages.
/// The observed window starts at construction (or reset()).
class CountingObserver : public IRouterObserver {
public:
    explicit CountingObserver(const IClock& clock);

    void onControlSent(std::uint8_t kind, bool broadcast, std::uint32_t bytes) override;
    void onControlReceived(std::uint8_t kind, std::uint32_t bytes) override;
    void onRouteChanged(NodeAddress dest, NodeAddress nextHop, bool added) override;
    void onDataDecision(NodeAddress dest, RouteAction action) override;
    void onStateSize(std::uint32_t entries, std::uint32_t bytes) override;

    /// The counters, with the time averages taken up to clock.now().
    RouterStats stats() const;
    /// Zeroes every counter and restarts the window at clock.now().
    void reset();

private:
    void accumulate(Time now);

    const IClock& clock_;
    RouterStats stats_;
    Time start_ = 0.0;
    Time last_ = 0.0;
    double entryArea_ = 0.0;
    double byteArea_ = 0.0;
};

}  // namespace swarm::core

#endif  // SWARM_CORE_OBSERVER_H
