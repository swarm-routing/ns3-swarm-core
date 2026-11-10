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
    /// monitored devices, fires LinkChange for its peer. SwarmRoutingBase calls
    /// it from NotifyInterfaceUp/Down; another Ipv4RoutingProtocol may too.
    void NotifyInterface(uint32_t interface, bool up);

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
    };

    static void DequeueThunk(IslLinkMonitor* self, std::size_t link, Ptr<const Packet> packet);
    void NotifyDequeue(std::size_t link);
    Ipv4Address PeerOf(const Link& link) const;
    int32_t InterfaceOf(const Link& link) const;
    const Link* LinkTowards(swarm::core::NodeAddress nextHop) const;

    Ptr<Node> m_node;
    std::vector<Link> m_links;
    double m_alpha;
    TracedCallback<Ipv4Address, bool> m_linkChangeTrace;
};

}  // namespace ns3

#endif  // SWARM_ISL_LINK_MONITOR_H
