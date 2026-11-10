// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet ns3/anthocnet/model/anthocnet-rqueue.cc
// and ns3/baselines/adaptive/model/adaptive-routing-base.cc @ 3e53f04
// (GPL-2.0-only); see swarm-deferred-packet-queue.h.
#include "swarm-deferred-packet-queue.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/uinteger.h"

#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("DeferredPacketQueue");
NS_OBJECT_ENSURE_REGISTERED(DeferredPacketQueue);

TypeId DeferredPacketQueue::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::DeferredPacketQueue")
            .SetParent<Object>()
            .SetGroupName("SwarmCore")
            .AddConstructor<DeferredPacketQueue>()
            .AddAttribute("MaxPackets",
                          "Packets held at once; a new one beyond it drops the oldest. "
                          "64, ns-3 AODV's default, so protocols buffer alike.",
                          UintegerValue(64),
                          MakeUintegerAccessor(&DeferredPacketQueue::m_maxPackets),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("MaxAge",
                          "How long a packet waits for a route, from its first enqueue, "
                          "before it is dropped. 30 s, ns-3 AODV's default.",
                          TimeValue(Seconds(30)), MakeTimeAccessor(&DeferredPacketQueue::m_maxAge),
                          MakeTimeChecker(Seconds(0)))
            .AddTraceSource("Drop",
                            "A packet left the queue without being forwarded "
                            "(packet, IP header, reason).",
                            MakeTraceSourceAccessor(&DeferredPacketQueue::m_dropTrace),
                            "ns3::DeferredPacketQueue::DropCallback");
    return tid;
}

DeferredPacketQueue::DeferredPacketQueue() : m_maxPackets(64), m_maxAge(Seconds(30)) {}

void DeferredPacketQueue::DoDispose() {
    m_queue.clear();
    Object::DoDispose();
}

bool DeferredPacketQueue::Expired(const DeferredPacket& e) const {
    return Simulator::Now() - e.enqueued >= m_maxAge;
}

void DeferredPacketQueue::Drop(const DeferredPacket& e, DeferredDropReason reason) {
    switch (reason) {
    case DeferredDropReason::Overflow:
        ++m_stats.overflow;
        break;
    case DeferredDropReason::Expired:
        ++m_stats.expired;
        break;
    case DeferredDropReason::Discarded:
        ++m_stats.discarded;
        break;
    }
    m_dropTrace(e.packet, e.header, reason);
    if (!e.ecb.IsNull()) {
        e.ecb(e.packet, e.header, Socket::ERROR_NOROUTETOHOST);
    }
}

void DeferredPacketQueue::Enqueue(DeferredPacket entry) {
    Purge();
    if (entry.enqueued.IsZero()) {
        entry.enqueued = Simulator::Now();
    }
    ++m_stats.enqueued;
    if (Expired(entry)) {
        Drop(entry, DeferredDropReason::Expired);  // came back after its time ran out
        return;
    }
    while (m_queue.size() >= m_maxPackets) {
        // Copy before the pop: the error callback runs after it, and may
        // re-enter the queue.
        const DeferredPacket oldest = m_queue.front();
        m_queue.pop_front();
        Drop(oldest, DeferredDropReason::Overflow);
    }
    m_queue.push_back(std::move(entry));
}

std::vector<DeferredPacket> DeferredPacketQueue::Release(Ipv4Address dst) {
    Purge();
    std::vector<DeferredPacket> out;
    std::deque<DeferredPacket> kept;
    for (auto& e : m_queue) {
        if (e.header.GetDestination() == dst) {
            out.push_back(e);
        } else {
            kept.push_back(e);
        }
    }
    m_queue.swap(kept);
    const Time now = Simulator::Now();
    for (const auto& e : out) {
        const double held = (now - e.enqueued).GetSeconds();
        ++m_stats.released;
        m_stats.releasedHoldSumS += held;
        m_stats.releasedHoldMaxS = std::max(m_stats.releasedHoldMaxS, held);
    }
    return out;
}

void DeferredPacketQueue::Discard(Ipv4Address dst) {
    std::deque<DeferredPacket> kept;
    std::vector<DeferredPacket> dropped;
    for (auto& e : m_queue) {
        if (e.header.GetDestination() == dst) {
            dropped.push_back(e);
        } else {
            kept.push_back(e);
        }
    }
    m_queue.swap(kept);
    for (const auto& e : dropped) {
        Drop(e, DeferredDropReason::Discarded);
    }
}

void DeferredPacketQueue::Purge() {
    std::deque<DeferredPacket> kept;
    std::vector<DeferredPacket> expired;
    for (auto& e : m_queue) {
        if (Expired(e)) {
            expired.push_back(e);
        } else {
            kept.push_back(e);
        }
    }
    m_queue.swap(kept);
    for (const auto& e : expired) {
        Drop(e, DeferredDropReason::Expired);
    }
}

bool DeferredPacketQueue::Has(Ipv4Address dst) const {
    return std::any_of(m_queue.begin(), m_queue.end(), [&](const DeferredPacket& e) {
        return e.header.GetDestination() == dst && !Expired(e);
    });
}

std::vector<Ipv4Address> DeferredPacketQueue::Destinations() const {
    std::vector<Ipv4Address> out;
    for (const auto& e : m_queue) {
        const Ipv4Address d = e.header.GetDestination();
        if (!Expired(e) && std::find(out.begin(), out.end(), d) == out.end()) {
            out.push_back(d);
        }
    }
    return out;
}

}  // namespace ns3
