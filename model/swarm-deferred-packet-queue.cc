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

DeferredPacketQueue::DeferredPacketQueue()
    : m_maxPackets(64), m_maxAge(Seconds(30)), m_oldest(Time::Max()) {}

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
    case DeferredDropReason::NodeDown:
        ++m_stats.nodeDown;
        break;
    }
    m_dropTrace(e.packet, e.header, reason);
    if (!e.ecb.IsNull()) {
        e.ecb(e.packet, e.header, Socket::ERROR_NOROUTETOHOST);
    }
}

void DeferredPacketQueue::Enqueue(DeferredPacket entry) {
    Purge();
    Admit(std::move(entry));
}

void DeferredPacketQueue::Requeue(std::vector<DeferredPacket> entries) {
    // One purge for the batch: time does not move inside a call, so the
    // per-entry Purge that Enqueue would run finds nothing more to drop.
    Purge();
    for (DeferredPacket& e : entries) {
        Admit(std::move(e));
    }
}

void DeferredPacketQueue::Admit(DeferredPacket entry) {
    if (entry.enqueued.IsZero()) {
        entry.enqueued = Simulator::Now();
    }
    ++m_stats.enqueued;
    if (Expired(entry)) {
        Drop(entry, DeferredDropReason::Expired);  // came back after its time ran out
        return;
    }
    while (m_queue.size() >= m_maxPackets) {
        // Move out before the pop: the error callback runs after it, and may
        // re-enter the queue.
        const DeferredPacket oldest = std::move(m_queue.front());
        m_queue.pop_front();
        Drop(oldest, DeferredDropReason::Overflow);
    }
    m_oldest = std::min(m_oldest, entry.enqueued);
    m_queue.push_back(std::move(entry));
}

template <typename Pred> std::vector<DeferredPacket> DeferredPacketQueue::Extract(Pred pred) {
    // Scan first: most calls (a purge with nothing expired, a release for a
    // destination with nothing held) remove nothing and touch nothing.
    std::vector<DeferredPacket> out;
    auto first = std::find_if(m_queue.begin(), m_queue.end(), pred);
    if (first == m_queue.end()) {
        return out;
    }
    // Stable, in place, by move: the kept entries close up in order.
    auto keep = first;
    for (auto it = first; it != m_queue.end(); ++it) {
        if (pred(*it)) {
            out.push_back(std::move(*it));
        } else {
            *keep++ = std::move(*it);
        }
    }
    m_queue.erase(keep, m_queue.end());
    return out;
}

std::vector<DeferredPacket> DeferredPacketQueue::Release(Ipv4Address dst) {
    Purge();
    std::vector<DeferredPacket> out =
        Extract([dst](const DeferredPacket& e) { return e.header.GetDestination() == dst; });
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
    for (const auto& e :
         Extract([dst](const DeferredPacket& p) { return p.header.GetDestination() == dst; })) {
        Drop(e, DeferredDropReason::Discarded);
    }
}

void DeferredPacketQueue::Purge() {
    // m_oldest is a lower bound on every held packet's enqueue time: while
    // even it is younger than MaxAge, nothing has expired and nothing is read.
    if (m_queue.empty() || Simulator::Now() - m_oldest < m_maxAge) {
        return;
    }
    for (const auto& e : Extract([this](const DeferredPacket& p) { return Expired(p); })) {
        Drop(e, DeferredDropReason::Expired);
    }
    m_oldest = Time::Max();
    for (const auto& e : m_queue) {
        m_oldest = std::min(m_oldest, e.enqueued);
    }
}

void DeferredPacketQueue::Flush() {
    // Out of the queue first: an error callback may re-enter it.
    std::deque<DeferredPacket> all;
    all.swap(m_queue);
    m_oldest = Time::Max();
    for (const auto& e : all) {
        Drop(e, DeferredDropReason::NodeDown);
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
