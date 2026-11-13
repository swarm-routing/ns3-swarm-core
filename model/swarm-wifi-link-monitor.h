// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// WifiLinkMonitor: the wifi MAC and PHY of one node, read through their trace
// sources only, as the core's ILinkState and ILinkQuality ports and a
// link-failure signal. Derived from swarm-routing/AntHocNet
// ns3/anthocnet/model/anthocnet-routing-protocol.cc @ 3e53f04 (GPL-2.0-only):
// the MAC backlog over every access category including AC_BE_NQOS (#73 there),
// the inter-ack service-time EWMA (#68), the per-neighbour SINR from
// MonitorSnifferRx (#181); and from ns3/baselines/adaptive/model/
// adaptive-routing-base.cc: the retry-limit drop as a broken link, resolved to
// the neighbour's IP through the ARP cache.
//
// A neighbour is known by its MAC address on the air and by its IP to the
// core. The monitor learns the pair from what it overhears: the sender fields
// of an ARP frame, and the SwarmPrevHopTag every SwarmRoutingBase stamps on
// the data it transmits; the node's ARP cache is the fallback. So a neighbour
// heard only through broadcasts and data has a SINR too, not only one this
// node has ARPed.
//
// Non-intrusive (ns3-tools' rule for probes): it only reads trace sources and
// const state, never schedules, draws or changes a packet, so a simulation's
// output is byte-identical with and without it (tested).
#ifndef SWARM_WIFI_LINK_MONITOR_H
#define SWARM_WIFI_LINK_MONITOR_H

#include "ns3/swarm-ports.h"

#include "ns3/ipv4-address.h"
#include "ns3/mac48-address.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-mpdu.h"
#include "ns3/wifi-net-device.h"

#include <map>
#include <vector>

namespace ns3 {

class Node;

class WifiLinkMonitor : public Object,
                        public swarm::core::ILinkState,
                        public swarm::core::ILinkQuality {
public:
    static TypeId GetTypeId();
    WifiLinkMonitor();

    /// Attaches to every WifiNetDevice of `node` and aggregates itself to the
    /// node (one monitor per node; SwarmRoutingBase finds it there). Call once
    /// the devices exist; addresses may come later, they are resolved per query.
    void Install(Ptr<Node> node);

    /// Packets waiting in the wifi MAC's queues, every access category. A
    /// single radio has one queue for every next hop, so `nextHop` is ignored.
    int macQueueLength(swarm::core::NodeAddress nextHop) const override;
    /// EWMA (seconds) of the spacing between acknowledgements while the MAC
    /// stayed backlogged: contention and retries included, queue wait not.
    /// 0 until the first sample.
    swarm::core::Time macServiceTime(swarm::core::NodeAddress nextHop) const override;
    /// SINR (dB) of the last frame heard from `neighbor`; NaN when none was, or
    /// its MAC address is not known yet.
    double sinrDbFrom(swarm::core::NodeAddress neighbor) const override;

    /// Retry-limit drops seen (link failures signalled, resolved or not).
    uint64_t LinkFailures() const { return m_failures; }

    /// Fired when the MAC gives up on a unicast frame (retry limit): the
    /// neighbour's IP (Ipv4Address() when ARP does not know it) and its MAC.
    typedef void (*LinkFailureCallback)(Ipv4Address neighbor, Mac48Address mac);
    /// Undelivered: the IPv4 datagram (IP header on, packet tags kept) of a
    /// unicast data frame the MAC gave up on at its retry limit, and the
    /// neighbour it was for (0.0.0.0 if ARP does not know it). A-MSDUs, MAC
    /// fragments and frames that carry no IPv4 are not reported (#40).
    typedef void (*UndeliveredCallback)(Ptr<const Packet> datagram, Ipv4Address neighbor);

protected:
    void DoDispose() override;

private:
    void NotifyDropped(WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu);
    void NotifyAcked(Ptr<const WifiMpdu> mpdu);
    void NotifySniffRx(Ptr<const Packet> packet, double sinrDb);
    int Backlog() const;
    Ipv4Address IpOf(Mac48Address mac) const;
    bool MacOf(Ipv4Address ip, Mac48Address& out) const;

    template <typename Fn> friend struct WifiSniffer;

    Ptr<Node> m_node;
    std::vector<Ptr<WifiNetDevice>> m_devices;
    double m_alpha;
    uint32_t m_maxNeighbors;
    double m_serviceEwmaS;
    Time m_lastAck;
    bool m_backlogAtLastAck;
    uint64_t m_failures;
    struct Neighbor {
        double sinrDb;   ///< of the last frame heard; NaN before one
        Ipv4Address ip;  ///< learned from ARP or SwarmPrevHopTag; 0.0.0.0 before
        Time at;         ///< when it was last heard
    };
    Neighbor& Heard(Mac48Address mac);
    std::map<Mac48Address, Neighbor> m_neighbors;  ///< bounded by MaxNeighbors
    TracedCallback<Ipv4Address, Mac48Address> m_linkFailureTrace;
    TracedCallback<Ptr<const Packet>, Ipv4Address> m_undeliveredTrace;
};

}  // namespace ns3

#endif  // SWARM_WIFI_LINK_MONITOR_H
