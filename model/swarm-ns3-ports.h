// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// ns-3 implementations of the core/ ports (core/swarm-ports.h): time from
// Simulator::Now, randomness from a UniformRandomVariable whose stream
// AssignStreams pins, deferred work from Simulator::Schedule. Derived from
// swarm-routing/AntHocNet ns3/anthocnet/model/anthocnet-adapters.{h,cc}
// @ 3e53f04 (GPL-2.0-only), where they adapt AntHocNet's own core; the timer
// scheduler and the address conversions are new here.
//
// Also the null ports SwarmRoutingBase hands out when a node has no monitor of
// that kind: every query answers "no evidence" (0, NaN, no neighbours), which
// the core's contracts already read as an unknown, never as a bad link.
#ifndef SWARM_NS3_PORTS_H
#define SWARM_NS3_PORTS_H

#include "ns3/swarm-ports.h"

#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>

namespace ns3 {

/// An ns-3 IPv4 address as a core address: the same 32 bits, reinterpreted.
swarm::core::NodeAddress ToNodeAddress(Ipv4Address address);
/// A core address back to ns-3. 0.0.0.0 and kInvalidAddress map to each
/// other; so does 255.255.255.255, whose 32 bits read as -1. Neither is ever a
/// node's address, so the round trip is exact for every real node.
Ipv4Address ToIpv4Address(swarm::core::NodeAddress address);

/// swarm::core::IClock over Simulator::Now(), in seconds.
class SwarmClock : public swarm::core::IClock {
public:
    swarm::core::Time now() const override;
};

/// swarm::core::IRng over one ns-3 UniformRandomVariable, so the program's
/// seed (SwarmCoreSeed) and AssignStreams fix every draw (org rule 5).
class SwarmRng : public swarm::core::IRng {
public:
    SwarmRng();
    double uniform() override;
    int uniformInt(int n) override;
    /// Pins the stream; returns the number of streams used (1).
    int64_t AssignStreams(int64_t stream);
    Ptr<UniformRandomVariable> Stream() const { return m_rng; }

private:
    Ptr<UniformRandomVariable> m_rng;
};

/// swarm::core::ITimerScheduler over Simulator::Schedule. CancelAll() drops
/// every callback not yet run (the owner calls it from DoDispose), without
/// keeping a list of events: each callback holds a shared token that
/// CancelAll invalidates, so the bookkeeping is O(1) however many are pending.
/// A cancellable timer (#46) is an ns-3 EventId kept until it runs or is
/// cancelled, so the map holds only pending timers (org rule 7); a cancelled
/// event is removed from the simulator's queue and never runs.
class SwarmTimerScheduler : public swarm::core::ITimerScheduler {
public:
    SwarmTimerScheduler();
    ~SwarmTimerScheduler() override;
    SwarmTimerScheduler(const SwarmTimerScheduler&) = delete;
    SwarmTimerScheduler& operator=(const SwarmTimerScheduler&) = delete;

    /// Runs `callback` after `delay` seconds; a negative delay runs it now.
    void schedule(swarm::core::Time delay, std::function<void()> callback) override;
    swarm::core::TimerHandle scheduleCancellable(swarm::core::Time delay,
                                                 std::function<void()> callback) override;
    void cancel(swarm::core::TimerHandle timer) override;
    /// Cancellable timers scheduled and not yet run or cancelled.
    std::size_t Pending() const { return m_pending.size(); }
    void CancelAll();

private:
    std::shared_ptr<bool> m_alive;
    swarm::core::TimerHandle m_next = swarm::core::kNoTimer;
    std::map<swarm::core::TimerHandle, EventId> m_pending;
};

/// No MAC signals: an empty queue, no service-time sample.
class NullLinkState : public swarm::core::ILinkState {
public:
    int macQueueLength(swarm::core::NodeAddress) const override { return 0; }
    swarm::core::Time macServiceTime(swarm::core::NodeAddress) const override { return 0.0; }
};

/// No link-quality samples: NaN for every neighbour.
class NullLinkQuality : public swarm::core::ILinkQuality {
public:
    double sinrDbFrom(swarm::core::NodeAddress) const override;
};

/// No energy model: NaN.
class NullEnergy : public swarm::core::IEnergy {
public:
    double residualEnergyFraction() const override;
};

/// No link-layer neighbour knowledge (a protocol learns its own, from hellos).
class NullNeighborProvider : public swarm::core::INeighborProvider {
public:
    std::vector<swarm::core::NodeAddress> neighbors() const override { return {}; }
};

}  // namespace ns3

#endif  // SWARM_NS3_PORTS_H
