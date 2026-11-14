// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// IslLinkMonitor: the point-to-point links (inter-satellite links, ISLs) of one
// node, as the core's INeighborProvider and ILinkState ports and a link up/down
// signal. Derived from swarm-routing/AntHocNet
// ns3/anthocnet/model/anthocnet-routing-protocol.cc @ 3e53f04 (GPL-2.0-only):
// the per-interface device-queue backlog and inter-dequeue service-time EWMA
// (#206 there) and the interface-down fast path for a scripted ISL break
// (#260): ns-3 cuts a point-to-point link with Ipv4::SetDown, which no device
// trace reports; Ipv4L3Protocol tells the routing protocol, and
// SwarmRoutingBase forwards it here (NotifyInterface).
//
// Non-intrusive: it reads trace sources and const state only (tested).
//
// Neighbour discovery without hello (#4): a point-to-point link has exactly one
// peer, known by construction, and its presence is an interface up/down event
// ns-3 already reports. IsAuthoritative(interface) says when that is enough:
// the interface sits on a monitored link with a known peer that has never gone
// down. A link that has failed once is no longer trusted (TrustAfterFailure,
// off by default), so hello comes back where links do change: AntHocNet's
// v2.3.0 `p2phello` campaign lost delivery on the ISL seam, where a throttled
// hello missed link changes (swarm-routing/AntHocNet#204 there, migrated as
// #4). SwarmRoutingBase reads it to suppress beacons per interface.
#ifndef SWARM_ISL_LINK_MONITOR_H
#define SWARM_ISL_LINK_MONITOR_H

#include "ns3/swarm-ports.h"

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/traced-callback.h"

#include <vector>

namespace ns3 {

class Node;

class IslLinkMonitor : public Object,
                       public swarm::core::INeighborProvider,
                       public swarm::core::ILinkState {
public:
    static TypeId GetTypeId();
    IslLinkMonitor();

    /// Attaches to every PointToPointNetDevice of `node` and aggregates itself
    /// to the node. Call once the devices exist; addresses are resolved per query.
    void Install(Ptr<Node> node);

    /// The peers of this node's point-to-point links that are up (device
    /// linked, local IPv4 interface up), in device order.
    std::vector<swarm::core::NodeAddress> neighbors() const override;
    /// Packets in the device transmit queue toward `nextHop`; the sum over every
    /// link for kInvalidAddress or a node that is no peer.
    int macQueueLength(swarm::core::NodeAddress nextHop) const override;
    /// EWMA (seconds) of the spacing between two dequeues of a backlogged
    /// device queue toward `nextHop`: serialisation only, never propagation.
    /// For kInvalidAddress, the mean over the links with a sample; 0 with none.
    swarm::core::Time macServiceTime(swarm::core::NodeAddress nextHop) const override;

    /// IPv4 interface `interface` went up or down: if it sits on one of the
    /// monitored devices, fires LinkChange for its peer (and, when down, marks
    /// the link as having failed: see IsAuthoritative). The far end loses the
    /// carrier too: its IslLinkMonitor, if it has one, marks the same link as
    /// failed and fires PeerLinkChange (ns-3's Ipv4::SetDown cuts one side
    /// only, so without this the far end would keep trusting a dead link,
    /// #4). SwarmRoutingBase calls it from NotifyInterfaceUp/Down; another
    /// Ipv4RoutingProtocol may too.
    void NotifyInterface(uint32_t interface, bool up);

    /// The peer of IPv4 interface `interface` (its address on the link's
    /// subnet), or Ipv4Address() when the interface is not on a monitored
    /// point-to-point device or the peer has no address. Known without any
    /// message: what a hello would otherwise teach (the interface toward a
    /// neighbour and its address there, #4).
    Ipv4Address PeerOnInterface(uint32_t interface) const;

    /// Whether link state alone gives the neighbour set of `interface`: it is
    /// on a monitored point-to-point device, its peer is known, and its link
    /// has never gone down (or TrustAfterFailure is set). False for every
    /// other interface: a shared medium (Wi-Fi, CSMA) keeps its hello (#4).
    bool IsAuthoritative(uint32_t interface) const;

    /// Fired when a monitored link goes up or down (peer IP, up).
    typedef void (*LinkChangeCallback)(Ipv4Address peer, bool up);

protected:
    void DoDispose() override;

private:
    struct Link {
        Ptr<PointToPointNetDevice> device;
        Time lastDequeue;
        bool backlogAtLastDequeue = false;
        double ewmaS = 0.0;
        bool failed = false;  ///< its interface has gone down at least once
    };

    static void DequeueThunk(IslLinkMonitor* self, std::size_t link, Ptr<const Packet> packet);
    void NotifyDequeue(std::size_t link);
    Ipv4Address PeerOf(const Link& link) const;
    int32_t InterfaceOf(const Link& link) const;
    const Link* LinkTowards(swarm::core::NodeAddress nextHop) const;
    const Link* LinkOn(uint32_t interface) const;
    /// The far end of `link` went up or down (its monitor's NotifyInterface).
    void FarEndChanged(Ptr<NetDevice> device, bool up);

    Ptr<Node> m_node;
    std::vector<Link> m_links;
    double m_alpha;
    bool m_trustAfterFailure;
    TracedCallback<Ipv4Address, bool> m_linkChangeTrace;
    TracedCallback<Ipv4Address, bool> m_peerLinkChangeTrace;
};

}  // namespace ns3

#endif  // SWARM_ISL_LINK_MONITOR_H
