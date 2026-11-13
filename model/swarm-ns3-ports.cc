// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet ns3/anthocnet/model/anthocnet-adapters.cc
// @ 3e53f04 (GPL-2.0-only); see swarm-ns3-ports.h.
#include "swarm-ns3-ports.h"

#include "ns3/simulator.h"

#include <limits>

namespace ns3 {

swarm::core::NodeAddress ToNodeAddress(Ipv4Address address) {
    if (address == Ipv4Address()) {
        return swarm::core::kInvalidAddress;
    }
    return static_cast<swarm::core::NodeAddress>(address.Get());
}

Ipv4Address ToIpv4Address(swarm::core::NodeAddress address) {
    if (address == swarm::core::kInvalidAddress) {
        return Ipv4Address();
    }
    return Ipv4Address(static_cast<uint32_t>(address));
}

swarm::core::Time SwarmClock::now() const {
    return Simulator::Now().GetSeconds();
}

SwarmRng::SwarmRng() : m_rng(CreateObject<UniformRandomVariable>()) {}

double SwarmRng::uniform() {
    // GetValue() draws from [min, max) = [0, 1) at the defaults.
    return m_rng->GetValue();
}

int SwarmRng::uniformInt(int n) {
    if (n <= 0) {
        return 0;  // the port's contract is n > 0; never let n - 1 wrap to 2^32 - 1
    }
    return static_cast<int>(m_rng->GetInteger(0, static_cast<uint32_t>(n - 1)));
}

int64_t SwarmRng::AssignStreams(int64_t stream) {
    m_rng->SetStream(stream);
    return 1;
}

SwarmTimerScheduler::SwarmTimerScheduler() : m_alive(std::make_shared<bool>(true)) {}

void SwarmTimerScheduler::schedule(swarm::core::Time delay, std::function<void()> callback) {
    const Time d = delay > 0.0 ? Seconds(delay) : Time(0);
    Simulator::Schedule(d, [alive = m_alive, cb = std::move(callback)]() {
        if (*alive) {
            cb();
        }
    });
}

SwarmTimerScheduler::~SwarmTimerScheduler() {
    *m_alive = false;  // a pending callback must not reach a destroyed scheduler
}

swarm::core::TimerHandle SwarmTimerScheduler::scheduleCancellable(swarm::core::Time delay,
                                                                  std::function<void()> callback) {
    const Time d = delay > 0.0 ? Seconds(delay) : Time(0);
    const swarm::core::TimerHandle handle = ++m_next;
    m_pending[handle] =
        Simulator::Schedule(d, [this, alive = m_alive, handle, cb = std::move(callback)]() {
            if (*alive) {
                m_pending.erase(handle);
                cb();
            }
        });
    return handle;
}

void SwarmTimerScheduler::cancel(swarm::core::TimerHandle timer) {
    const auto it = m_pending.find(timer);
    if (it == m_pending.end()) {
        return;
    }
    Simulator::Remove(it->second);  // out of the queue, with its callback
    m_pending.erase(it);
}

void SwarmTimerScheduler::CancelAll() {
    *m_alive = false;
    m_alive = std::make_shared<bool>(true);
    m_pending.clear();
}

double NullLinkQuality::sinrDbFrom(swarm::core::NodeAddress) const {
    return std::numeric_limits<double>::quiet_NaN();
}

double NullEnergy::residualEnergyFraction() const {
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace ns3
