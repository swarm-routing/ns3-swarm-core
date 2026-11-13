// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet ns3/anthocnet/model/anthocnet-routing-protocol.cc
// and ns3/baselines/adaptive/model/adaptive-routing-base.cc @ 3e53f04
// (GPL-2.0-only); see swarm-wifi-link-monitor.h.
#include "swarm-wifi-link-monitor.h"

#include "swarm-ns3-ports.h"
#include "swarm-routing-base.h"

#include "ns3/arp-cache.h"
#include "ns3/arp-header.h"
#include "ns3/double.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/llc-snap-header.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-mac-queue.h"
#include "ns3/wifi-phy.h"

#include <cmath>
#include <limits>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("WifiLinkMonitor");
NS_OBJECT_ENSURE_REGISTERED(WifiLinkMonitor);

namespace {

/// EtherType of ARP in the LLC/SNAP header (the ns-3 constant moved between
/// releases).
constexpr uint16_t kArpEtherType = 0x0806;
/// An ARP packet for IPv4 over 6-byte MAC addresses.
constexpr uint32_t kArpIpv4Size = 28;

}  // namespace

/// What MonitorSnifferRx hands over, picked out by type: the received MPDU and
/// its signal/noise. The trace's argument list has changed across ns-3
/// releases, so the sniffer takes it from WifiPhy::MonitorSnifferRxCallback
/// instead of spelling it out (AntHocNet's PhySniffer, #181 there).
struct WifiSniffSample {
    Ptr<const Packet> packet;
    double sinrDb = std::numeric_limits<double>::quiet_NaN();
};

inline void Pick(WifiSniffSample& s, Ptr<const Packet> p) {
    s.packet = p;
}

inline void Pick(WifiSniffSample& s, const SignalNoiseDbm& sn) {
    // ns-3 sets noise = rx power / SNR, where the SNR includes interference
    // (PhyEntity::GetReceptionStatus), so signal - noise is the SINR in dB.
    s.sinrDb = sn.signal - sn.noise;
}

template <typename T> inline void Pick(WifiSniffSample&, const T&) {}

template <typename Fn> struct WifiSniffer;

template <typename... A> struct WifiSniffer<void (*)(A...)> {
    static void Fire(WifiLinkMonitor* m, A... args) {
        WifiSniffSample s;
        (Pick(s, args), ...);
        if (s.packet) {
            m->NotifySniffRx(s.packet, s.sinrDb);
        }
    }

    static void Connect(Ptr<WifiPhy> phy, WifiLinkMonitor* m) {
        phy->TraceConnectWithoutContext("MonitorSnifferRx", MakeBoundCallback(&Fire, m));
    }
};

TypeId WifiLinkMonitor::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::WifiLinkMonitor")
            .SetParent<Object>()
            .SetGroupName("SwarmCore")
            .AddConstructor<WifiLinkMonitor>()
            .AddAttribute("ServiceTimeAlpha",
                          "Weight of the previous estimate in the service-time EWMA "
                          "(0.9, AntHocNet's MacServiceAlpha).",
                          DoubleValue(0.9), MakeDoubleAccessor(&WifiLinkMonitor::m_alpha),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("MaxNeighbors",
                          "Neighbours whose last SINR and IP are kept; a new one beyond "
                          "it replaces the one heard longest ago.",
                          UintegerValue(256),
                          MakeUintegerAccessor(&WifiLinkMonitor::m_maxNeighbors),
                          MakeUintegerChecker<uint32_t>(1))
            .AddTraceSource("LinkFailure",
                            "The MAC gave up on a unicast frame at its retry limit "
                            "(neighbour IP, or 0.0.0.0 if ARP does not know it; MAC).",
                            MakeTraceSourceAccessor(&WifiLinkMonitor::m_linkFailureTrace),
                            "ns3::WifiLinkMonitor::LinkFailureCallback")
            .AddTraceSource("Undelivered",
                            "The IPv4 datagram of a unicast data frame the MAC gave up on "
                            "at its retry limit (IP header on), and the neighbour IP.",
                            MakeTraceSourceAccessor(&WifiLinkMonitor::m_undeliveredTrace),
                            "ns3::WifiLinkMonitor::UndeliveredCallback");
    return tid;
}

WifiLinkMonitor::WifiLinkMonitor()
    : m_alpha(0.9), m_maxNeighbors(256), m_serviceEwmaS(0.0), m_backlogAtLastAck(false),
      m_failures(0) {}

void WifiLinkMonitor::DoDispose() {
    m_node = nullptr;
    m_devices.clear();
    m_neighbors.clear();
    Object::DoDispose();
}

void WifiLinkMonitor::Install(Ptr<Node> node) {
    NS_ASSERT_MSG(!m_node, "WifiLinkMonitor::Install called twice");
    m_node = node;
    for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
        Ptr<WifiNetDevice> dev = node->GetDevice(i)->GetObject<WifiNetDevice>();
        if (!dev || !dev->GetMac()) {
            continue;
        }
        m_devices.push_back(dev);
        dev->GetMac()->TraceConnectWithoutContext(
            "DroppedMpdu", MakeCallback(&WifiLinkMonitor::NotifyDropped, this));
        dev->GetMac()->TraceConnectWithoutContext(
            "AckedMpdu", MakeCallback(&WifiLinkMonitor::NotifyAcked, this));
        if (dev->GetPhy()) {
            WifiSniffer<WifiPhy::MonitorSnifferRxCallback>::Connect(dev->GetPhy(), this);
        }
    }
    node->AggregateObject(this);
}

int WifiLinkMonitor::Backlog() const {
    uint32_t total = 0;
    for (const auto& dev : m_devices) {
        // AC_BE_NQOS is essential: a non-QoS MAC (ad hoc, the MANET default)
        // keeps its one DCF queue there, not under AC_BE (AntHocNet #73).
        for (AcIndex ac : {AC_BE_NQOS, AC_BE, AC_BK, AC_VI, AC_VO}) {
            Ptr<WifiMacQueue> q = dev->GetMac()->GetTxopQueue(ac);
            if (q) {
                total += q->GetNPackets();
            }
        }
    }
    return static_cast<int>(total);
}

int WifiLinkMonitor::macQueueLength(swarm::core::NodeAddress) const {
    return Backlog();
}

swarm::core::Time WifiLinkMonitor::macServiceTime(swarm::core::NodeAddress) const {
    return m_serviceEwmaS;
}

void WifiLinkMonitor::NotifyAcked(Ptr<const WifiMpdu> mpdu) {
    // A pure service sample is the spacing between two acknowledgements with
    // the MAC never idle in between: the queue was still backlogged at the
    // previous one (AntHocNet #68).
    const Time now = Simulator::Now();
    if (m_backlogAtLastAck && m_lastAck.IsStrictlyPositive()) {
        const double sample = (now - m_lastAck).GetSeconds();
        if (sample > 0.0) {
            m_serviceEwmaS =
                m_serviceEwmaS > 0.0 ? m_alpha * m_serviceEwmaS + (1.0 - m_alpha) * sample : sample;
        }
    }
    m_lastAck = now;
    // The acknowledged MPDU is still in its queue while this trace fires (the
    // MAC removes it afterwards), so it must not count as backlog: with it, an
    // idle gap after a lone frame would be sampled as one huge service time.
    const int self = mpdu && mpdu->IsQueued() ? 1 : 0;
    m_backlogAtLastAck = Backlog() - self > 0;
}

void WifiLinkMonitor::NotifyDropped(WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu) {
    // Only a retry-limit drop is a broken link; the other reasons are queue
    // overflow or age, i.e. congestion.
    if (!mpdu || reason != WIFI_MAC_DROP_REACHED_RETRY_LIMIT) {
        return;
    }
    const Mac48Address to = mpdu->GetHeader().GetAddr1();
    if (to.IsGroup()) {
        return;
    }
    ++m_failures;
    m_linkFailureTrace(IpOf(to), to);
    // The datagram itself, so a protocol can send it another way (#40): the
    // MSDU of a data frame is LLC/SNAP + the IPv4 datagram.
    const WifiMacHeader& mac = mpdu->GetHeader();
    LlcSnapHeader llc;
    if (mac.IsData() && !(mac.IsQosData() && mac.IsQosAmsdu()) &&
        mpdu->GetPacket()->GetSize() > llc.GetSerializedSize()) {
        Ptr<Packet> datagram = mpdu->GetPacket()->Copy();
        datagram->RemoveHeader(llc);
        if (llc.GetType() == 0x0800) {  // IPv4's EtherType (PROT_NUMBER is deprecated in 3.49)
            m_undeliveredTrace(datagram, IpOf(to));
        }
    }
}

WifiLinkMonitor::Neighbor& WifiLinkMonitor::Heard(Mac48Address mac) {
    auto it = m_neighbors.find(mac);
    if (it == m_neighbors.end()) {
        if (m_neighbors.size() >= m_maxNeighbors) {
            auto oldest = m_neighbors.begin();
            for (auto j = m_neighbors.begin(); j != m_neighbors.end(); ++j) {
                if (j->second.at < oldest->second.at) {
                    oldest = j;
                }
            }
            m_neighbors.erase(oldest);
        }
        it = m_neighbors
                 .emplace(mac, Neighbor{std::numeric_limits<double>::quiet_NaN(), Ipv4Address(),
                                        Simulator::Now()})
                 .first;
    }
    it->second.at = Simulator::Now();
    return it->second;
}

void WifiLinkMonitor::NotifySniffRx(Ptr<const Packet> packet, double sinrDb) {
    // 10 bytes: the shortest MAC frame (ACK, CTS), so the peek stays in bounds.
    WifiMacHeader hdr;
    if (packet->GetSize() < 10 || packet->PeekHeader(hdr) == 0 || hdr.IsCtl()) {
        return;  // control frames (ACK, CTS) carry no transmitter address
    }
    Neighbor& n = Heard(hdr.GetAddr2());
    if (!std::isnan(sinrDb)) {
        n.sinrDb = sinrDb;
    }
    // Learn the transmitter's IP: a SwarmPrevHopTag names it exactly (packet
    // tags travel with the frame); so do an ARP frame's sender fields.
    SwarmPrevHopTag tag;
    if (packet->PeekPacketTag(tag) && tag.Get() != Ipv4Address()) {
        n.ip = tag.Get();
        return;
    }
    if (!hdr.IsData()) {
        return;
    }
    Ptr<Packet> body = packet->Copy();
    body->RemoveHeader(hdr);
    LlcSnapHeader llc;
    ArpHeader arp;
    if (body->GetSize() >= llc.GetSerializedSize() && body->RemoveHeader(llc) != 0 &&
        llc.GetType() == kArpEtherType && body->GetSize() >= kArpIpv4Size &&
        body->PeekHeader(arp) != 0 &&
        Mac48Address::IsMatchingType(arp.GetSourceHardwareAddress()) &&
        Mac48Address::ConvertFrom(arp.GetSourceHardwareAddress()) == hdr.GetAddr2()) {
        n.ip = arp.GetSourceIpv4Address();
    }
}

double WifiLinkMonitor::sinrDbFrom(swarm::core::NodeAddress neighbor) const {
    Mac48Address mac;
    if (!MacOf(ToIpv4Address(neighbor), mac)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    auto it = m_neighbors.find(mac);
    return it == m_neighbors.end() ? std::numeric_limits<double>::quiet_NaN() : it->second.sinrDb;
}

Ipv4Address WifiLinkMonitor::IpOf(Mac48Address mac) const {
    if (auto it = m_neighbors.find(mac);
        it != m_neighbors.end() && it->second.ip != Ipv4Address()) {
        return it->second.ip;
    }
    Ptr<Ipv4L3Protocol> l3 = m_node ? m_node->GetObject<Ipv4L3Protocol>() : nullptr;
    if (!l3) {
        return Ipv4Address();
    }
    for (uint32_t i = 0; i < l3->GetNInterfaces(); ++i) {
        Ptr<ArpCache> cache = l3->GetInterface(i)->GetArpCache();
        if (!cache) {
            continue;
        }
        for (ArpCache::Entry* e : cache->LookupInverse(mac)) {
            if (e && e->GetIpv4Address() != Ipv4Address()) {
                return e->GetIpv4Address();
            }
        }
    }
    return Ipv4Address();
}

bool WifiLinkMonitor::MacOf(Ipv4Address ip, Mac48Address& out) const {
    if (ip == Ipv4Address()) {
        return false;
    }
    for (const auto& [mac, n] : m_neighbors) {
        if (n.ip == ip) {
            out = mac;
            return true;
        }
    }
    Ptr<Ipv4L3Protocol> l3 = m_node ? m_node->GetObject<Ipv4L3Protocol>() : nullptr;
    if (!l3) {
        return false;
    }
    for (uint32_t i = 0; i < l3->GetNInterfaces(); ++i) {
        Ptr<ArpCache> cache = l3->GetInterface(i)->GetArpCache();
        ArpCache::Entry* e = cache ? cache->Lookup(ip) : nullptr;
        if (e && (e->IsAlive() || e->IsPermanent() || e->IsAutoGenerated())) {
            const Address a = e->GetMacAddress();
            if (Mac48Address::IsMatchingType(a)) {
                out = Mac48Address::ConvertFrom(a);
                return true;
            }
        }
    }
    return false;
}

}  // namespace ns3
