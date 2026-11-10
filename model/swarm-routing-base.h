// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// SwarmRoutingBase: the ns-3 plumbing every swarm-routing protocol repeats, as
// an Ipv4RoutingProtocol a protocol's adapter derives from. It holds no routing
// policy (golden rule 11): the adapter converts its headers and carries out
// its core's decisions through the hooks below; the base owns the rest.
//
// - the core's ports over ns-3: Clock(), Rng() (AssignStreams pins it),
//   Timers() (cancelled on dispose), and the node's monitors when installed
//   (LinkState(), LinkQuality(), Neighbors(), Energy(); null ports otherwise);
// - one UDP control socket per interface (unicast + subnet broadcast) on the
//   protocol's port, so harnesses count control traffic as for every protocol;
// - the deferred route: a data packet with no next hop is bounced through
//   loopback, held in a DeferredPacketQueue with its forwarding callbacks, and
//   released when the protocol learns a route (or dropped through its error
//   callback, which FlowMonitor books as a route drop);
// - the previous hop of every data packet (SwarmPrevHopTag), which ns-3's
//   RouteInput does not expose and per-hop learning needs;
// - link failures from the node's WifiLinkMonitor (MAC retry limit) and
//   IslLinkMonitor (interface down), delivered to OnLinkFailure.
//
// Derived from swarm-routing/AntHocNet
// ns3/baselines/adaptive/model/adaptive-routing-base.{h,cc} @ 3e53f04
// (GPL-2.0-only; the RoutingBase its ARA, Termite, Q-routing and BeeAdHoc arms
// share, ADR-0027 there). Its MAC/ARP packet recovery, backtrack tag and
// per-reason hold accounting stay behind with those arms (golden rule 11);
// link-failure detection moved to the monitors.
#ifndef SWARM_ROUTING_BASE_H
#define SWARM_ROUTING_BASE_H

#include "swarm-deferred-packet-queue.h"
#include "swarm-ns3-ports.h"

#include "ns3/swarm-route-decision.h"

#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/mac48-address.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/tag.h"
#include "ns3/timer.h"

#include <map>

namespace ns3 {

/**
 * The IP address of the node that last transmitted this data packet. A real
 * implementation reads it from the link layer's source address; ns-3's
 * RouteInput passes the incoming device but not that address, so the sender
 * stamps itself. A packet tag is simulator metadata: no bytes on the air,
 * like the MAC source address it stands in for.
 */
class SwarmPrevHopTag : public Tag {
public:
    SwarmPrevHopTag() = default;
    explicit SwarmPrevHopTag(Ipv4Address sender) : m_sender(sender) {}

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;
    void Print(std::ostream& os) const override;

    Ipv4Address Get() const { return m_sender; }

private:
    Ipv4Address m_sender;
};

class SwarmRoutingBase : public Ipv4RoutingProtocol {
public:
    static TypeId GetTypeId();
    SwarmRoutingBase();
    ~SwarmRoutingBase() override;

    /// Pins the protocol's randomness (the Rng() port); returns the streams
    /// used. A protocol with more random variables overrides it and chains up.
    virtual int64_t AssignStreams(int64_t stream);

    Ptr<DeferredPacketQueue> GetDeferredQueue() const { return m_queue; }

    // Ipv4RoutingProtocol
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header& header, Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;
    bool RouteInput(Ptr<const Packet> p, const Ipv4Header& header, Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb, const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb, const ErrorCallback& ecb) override;
    void NotifyInterfaceUp(uint32_t interface) override;
    void NotifyInterfaceDown(uint32_t interface) override;
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void SetIpv4(Ptr<Ipv4> ipv4) override;
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

protected:
    void DoInitialize() override;
    void DoDispose() override;

    // --- hooks: the protocol's adapter -----------------------------------------

    /// The UDP port of the protocol's control messages.
    virtual uint16_t ControlPort() const = 0;

    /// Next hop for a data packet to `dst` from `src` that arrived from `prev`
    /// (Ipv4Address() for a packet this node originates). Ipv4Address() when
    /// the protocol has none: the base then holds the packet and calls
    /// StartDiscovery.
    virtual Ipv4Address SelectNextHop(Ipv4Address dst, Ipv4Address src, Ipv4Address prev,
                                      Ptr<const Packet> p) = 0;

    /// The deferred queue holds data for `dst` and there is no route: look.
    virtual void StartDiscovery(Ipv4Address dst) = 0;

    /// A control message arrived from one-hop neighbour `from` on the
    /// interface whose address is `receiver`.
    virtual void RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address receiver) = 0;

    /// A data packet arrived from neighbour `prev` (every hop, the destination
    /// included). Return false to consume it (the protocol dealt with it).
    virtual bool OnDataReceived(Ptr<const Packet> /*p*/, const Ipv4Header& /*header*/,
                                Ipv4Address /*prev*/) {
        return true;
    }

    /// A data packet is being handed to neighbour `next`.
    virtual void OnDataForwarded(Ptr<const Packet> /*p*/, const Ipv4Header& /*header*/,
                                 Ipv4Address /*next*/) {}

    /// The link to `neighbor` failed (MAC retry limit, or its interface went down).
    virtual void OnLinkFailure(Ipv4Address /*neighbor*/) {}

    /// A point-to-point link to `neighbor` came up.
    virtual void OnLinkUp(Ipv4Address /*neighbor*/) {}

    // --- services ----------------------------------------------------------------

    /// One-hop broadcast of a control message on every interface.
    void BroadcastControl(Ptr<Packet> p);
    /// One-hop unicast of a control message to a neighbour.
    void UnicastControl(Ptr<Packet> p, Ipv4Address neighbor);
    /// Forwards a data packet to `next` (stamps SwarmPrevHopTag).
    void ForwardData(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address next,
                     const UnicastForwardCallback& ucb);
    /// Sends every held packet to `dst` that now has a next hop; the rest go
    /// back to the queue, keeping their first enqueue time.
    void ReleaseDeferred(Ipv4Address dst);
    /// Drops every held packet to `dst` (a core's DiscardPending).
    void DiscardDeferred(Ipv4Address dst);

    /// Carries out the decisions a core returns that need no data packet in
    /// hand: Unicast and Broadcast of a control message (`encode` turns the
    /// core's message into the packet the protocol sends) and DiscardPending.
    /// Queue, Deliver and Drop are about the data packet RouteInput holds, so
    /// the adapter handles them there. Returns false for those.
    template <typename Message, typename Encode>
    bool Execute(const swarm::core::RouteDecision<Message>& d, Encode encode) {
        using swarm::core::RouteAction;
        switch (d.action) {
        case RouteAction::Unicast:
            if (d.message) {
                UnicastControl(encode(*d.message), ToIpv4Address(d.nextHop));
            }
            return true;
        case RouteAction::Broadcast:
            if (d.message) {
                BroadcastControl(encode(*d.message));
            }
            return true;
        case RouteAction::DiscardPending:
            DiscardDeferred(ToIpv4Address(d.nextHop));
            return true;
        case RouteAction::None:
            return true;
        default:
            return false;
        }
    }

    Ipv4Address MainAddress() const;
    bool IsMyAddress(Ipv4Address a) const;

    // --- the core's ports ------------------------------------------------------

    const swarm::core::IClock& Clock() const { return m_clock; }
    SwarmRng& Rng() { return m_rng; }
    swarm::core::ITimerScheduler& Timers() { return m_timers; }
    /// The node's WifiLinkMonitor, else its IslLinkMonitor, else a null port.
    const swarm::core::ILinkState& LinkState() const;
    /// The node's WifiLinkMonitor, else a null port.
    const swarm::core::ILinkQuality& LinkQuality() const;
    /// The node's IslLinkMonitor, else a null port.
    const swarm::core::INeighborProvider& Neighbors() const;
    /// The node's EnergyMetricHook, else a null port.
    const swarm::core::IEnergy& Energy() const;

private:
    Ptr<Node> NodeOf() const;
    Ptr<Ipv4Route> LoopbackRoute(const Ipv4Header& header, Ptr<NetDevice> oif) const;
    Ptr<Ipv4Route> DirectRoute(Ipv4Address dst, Ipv4Address next, Ipv4Address src) const;
    int32_t InterfaceTowards(Ipv4Address next) const;
    void Defer(Ptr<const Packet> p, const Ipv4Header& header, const UnicastForwardCallback& ucb,
               const ErrorCallback& ecb);
    void PurgeTick();
    void RecvSocket(Ptr<Socket> socket);
    void WifiLinkFailed(Ipv4Address neighbor, Mac48Address mac);
    void IslLinkChanged(Ipv4Address peer, bool up);

    Ptr<Ipv4> m_ipv4;
    Ptr<NetDevice> m_lo;
    std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_sockets;
    std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_bcastSockets;
    Ptr<DeferredPacketQueue> m_queue;
    Time m_purgeInterval;
    Timer m_purgeTimer;
    SwarmClock m_clock;
    SwarmRng m_rng;
    SwarmTimerScheduler m_timers;
    /// Set across a control SendTo, so RouteOutput answers with a one-hop
    /// route to the neighbour instead of asking the protocol.
    Ipv4Address m_directTo;
};

}  // namespace ns3

#endif  // SWARM_ROUTING_BASE_H
