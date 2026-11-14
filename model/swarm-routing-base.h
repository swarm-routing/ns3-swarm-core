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
//   IslLinkMonitor (interface down), delivered to OnLinkFailure;
// - the router observer (core/swarm-observer.h, #42): Observer() is the
//   IRouterObserver the adapter hands its core; it feeds the ControlTx,
//   ControlRx, RouteChanged, DataDecision and StateSize trace sources and
//   bounded per-node counters (GetRouterStats), and only reads (org rule 5);
// - neighbour discovery from link state on point-to-point links (#4): with
//   IslHelloSuppression on, BroadcastBeacon skips every interface the node's
//   IslLinkMonitor calls authoritative (one known peer, never failed), the
//   peers of those interfaces are announced through OnLinkUp at start and
//   restart, and IsLinkStateNeighbor tells the adapter which neighbours it
//   must not age out for missing hellos. Off by default: nothing changes.
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

#include "ns3/swarm-observer.h"
#include "ns3/swarm-route-decision.h"

#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/mac48-address.h"
#include "ns3/event-id.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/tag.h"
#include "ns3/timer.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
    /// `from`: the hop the sender had the packet from (0.0.0.0 when the
    /// sender originated it), so a sender whose MAC gives the packet back
    /// still knows its own previous hop (OnDataUndelivered, #40).
    explicit SwarmPrevHopTag(Ipv4Address sender, Ipv4Address from = Ipv4Address())
        : m_sender(sender), m_from(from) {}

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;
    void Print(std::ostream& os) const override;

    Ipv4Address Get() const { return m_sender; }
    Ipv4Address GetFrom() const { return m_from; }

private:
    Ipv4Address m_sender;
    Ipv4Address m_from;
};

/// A node's lifecycle state (#43).
enum class SwarmNodeState : uint8_t {
    Up = 0,
    Down = 1,
};

/// The lifecycle operations, after INET's NodeStartOperation,
/// NodeShutdownOperation and NodeCrashOperation (#43).
enum class SwarmLifecycleOp : uint8_t {
    Start = 0,     ///< the node comes (back) up
    Shutdown = 1,  ///< graceful: the protocol may announce it is leaving
    Crash = 2,     ///< abrupt: nothing is sent, peers are not told
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

    // --- node lifecycle (#43) -------------------------------------------------

    /// Graceful shutdown: DoNodeShutdown runs first, while the node is still
    /// up (so the protocol can send its leaving control), then the base
    /// cancels every timer it owns (Timers() and the purge timer), drops every
    /// deferred packet as NodeDown, and refuses routing until Start. A no-op
    /// when already down. Completes in zero simulated time.
    void Shutdown();
    /// Crash: the node is down at once, so DoNodeCrash can send nothing; then
    /// the same teardown as Shutdown. A no-op when already down.
    void Crash();
    /// Brings a down node back: routing is accepted again, the purge timer
    /// restarts, then DoNodeStart re-initialises the protocol. A no-op when up.
    void Start();
    SwarmNodeState GetNodeState() const { return m_state; }
    bool IsDown() const { return m_state == SwarmNodeState::Down; }

    /// Lifecycle: the node changed state (`state`, by `op`).
    using LifecycleTracedCallback = void (*)(SwarmNodeState state, SwarmLifecycleOp op);

    // --- the router observer ---------------------------------------------------

    /// ControlTx: a control message of `kind` (the protocol's wire type byte)
    /// left this node, `bytes` long.
    using ControlTxTracedCallback = void (*)(uint8_t kind, bool broadcast, uint32_t bytes);
    /// ControlRx: this node processed a received control message of `kind`.
    using ControlRxTracedCallback = void (*)(uint8_t kind, uint32_t bytes);
    /// RouteChanged: a route to `dest` through `nextHop` was added or removed.
    using RouteChangedTracedCallback = void (*)(Ipv4Address dest, Ipv4Address nextHop, bool added);
    /// DataDecision: what the core decided for a data packet to `dest`.
    using DataDecisionTracedCallback = void (*)(Ipv4Address dest, swarm::core::RouteAction action);
    /// StateSize: the protocol's routing-state footprint.
    using StateSizeTracedCallback = void (*)(uint32_t entries, uint32_t bytes);

    /// This node's observer counters, time averages taken up to now. The
    /// window starts when the protocol is created, or at ResetRouterStats.
    swarm::core::RouterStats GetRouterStats() const;
    void ResetRouterStats();

    /// Names the control kinds of `protocol` (its TypeId) for readers; a
    /// protocol registers its static KindName table once. A later call
    /// replaces the names. At most 256 kinds, the width of the byte.
    static void SetKindNames(TypeId protocol, std::span<const swarm::core::KindName> names);
    /// The name of `kind` for `protocol` or its nearest registered parent,
    /// else the kind number in decimal.
    static std::string GetKindName(TypeId protocol, uint8_t kind);
    /// The name of `kind` for this protocol.
    std::string KindName(uint8_t kind) const;

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
    /// Marks the cached ports stale, so a monitor aggregated to the node
    /// later is still seen (#46). An adapter overriding it chains up.
    void NotifyNewAggregate() override;

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

    /// The link to `neighbor` failed (MAC retry limit, or its interface went
    /// down).
    virtual void OnLinkFailure(Ipv4Address /*neighbor*/) {}

    /// A point-to-point link to `neighbor` came up. With IslHelloSuppression
    /// on, also called once per authoritative interface's peer at start (an
    /// event at time 0 after DoInitialize) and after DoNodeStart: the
    /// neighbour-learn path a hello would otherwise feed (#4).
    virtual void OnLinkUp(Ipv4Address /*neighbor*/) {}

    /// The Wi-Fi MAC gave up on data packet `p` (IP header removed, `header`
    /// its header) at its retry limit toward `failedNext`; `prev` is the hop
    /// this node had it from (Ipv4Address() if it originated here). The
    /// default does nothing: the packet is lost, as before. A protocol that
    /// re-routes or sends it back (ARA's backtracking) calls ResendData. Only
    /// a WifiLinkMonitor reports this; OnLinkFailure still fires first (#40).
    virtual void OnDataUndelivered(Ptr<const Packet> /*p*/, const Ipv4Header& /*header*/,
                                   Ipv4Address /*failedNext*/, Ipv4Address /*prev*/) {}

    /// Lifecycle hooks (#43). Each defaults to nothing; the base does the
    /// common part (timers, deferred queue, refusing routing) around them.
    /// DoNodeShutdown: the node is still up; send the protocol's leaving
    /// control here if it has one, then clear the protocol's state.
    virtual void DoNodeShutdown() {}
    /// DoNodeCrash: the node is already down (nothing it sends leaves); clear
    /// the protocol's state.
    virtual void DoNodeCrash() {}
    /// DoNodeStart: the node is up again; re-initialise and restart periodic
    /// timers, with fresh jitter drawn from Rng() (org rule 5).
    virtual void DoNodeStart() {}

    // --- services
    // ----------------------------------------------------------------

    /// One-hop broadcast of a control message on every interface. Nothing
    /// is sent while the node is down; neither is a unicast.
    void BroadcastControl(Ptr<Packet> p);
    /// One-hop broadcast of a beacon: a control message whose only role is
    /// liveness (a hello). It goes out as BroadcastControl does, except on the
    /// interfaces where BeaconSuppressed is true. Returns the number of
    /// interfaces it was sent on, so the adapter reports to its observer only
    /// a beacon that left. A hello that carries anything else (pheromone
    /// diffusion adverts, a neighbour's canonical address) is not a beacon:
    /// send that part with BroadcastControl, or on its own timer (#4).
    uint32_t BroadcastBeacon(Ptr<Packet> p);
    /// Whether beacons skip IPv4 interface `interface`: IslHelloSuppression is
    /// on and the node's IslLinkMonitor gives that interface's neighbour from
    /// link state (IslLinkMonitor::IsAuthoritative). Always false when the
    /// attribute is off or the node has no IslLinkMonitor.
    bool BeaconSuppressed(uint32_t interface) const;
    /// Whether `neighbor` is the peer of an interface whose beacons are
    /// suppressed: its presence comes from link state (OnLinkUp,
    /// OnLinkFailure), so a hello-loss detector must not age it out. False
    /// once its link has failed (hello takes over again) or with the
    /// attribute off.
    bool IsLinkStateNeighbor(Ipv4Address neighbor) const;
    /// One-hop unicast of a control message to a neighbour.
    void UnicastControl(Ptr<Packet> p, Ipv4Address neighbor);
    /// Forwards a data packet to `next` (stamps SwarmPrevHopTag).
    void ForwardData(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address next,
                     const UnicastForwardCallback& ucb);
    /// Sends data packet `p` (from OnDataUndelivered) to neighbour `next`,
    /// as ForwardData does; its TTL drops by one more hop. Calls no hook: the
    /// protocol records what it wants. False when it cannot be sent (no
    /// forwarding callback seen yet, or no interface toward `next`).
    bool ResendData(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address next);
    /// Sends every held packet to `dst` that now has a next hop; the rest go
    /// back to the queue, keeping their first enqueue time.
    void ReleaseDeferred(Ipv4Address dst);
    /// Drops every held packet to `dst` (a core's DiscardPending).
    void DiscardDeferred(Ipv4Address dst);
    /// Holds a data packet RouteInput has no next hop for, then calls
    /// StartDiscovery(dst). The packet is held as given, so a protocol header
    /// the adapter put on it stays on: an adapter that overrides RouteInput
    /// (Termite's, whose header rides on every data packet) holds its packets
    /// here too. ReleaseDeferred hands it, unchanged, to SelectNextHop,
    /// OnDataForwarded and ForwardData (swarm-routing/ns3-swarm-core#40).
    void Defer(Ptr<const Packet> p, const Ipv4Header& header, const UnicastForwardCallback& ucb,
               const ErrorCallback& ecb);

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

    /// The router observer the adapter hands its core (as an
    /// IRouterObserver*), and calls itself for what only the adapter sees.
    swarm::core::IRouterObserver& Observer() { return m_observer; }

    // --- the core's ports ------------------------------------------------------

    const swarm::core::IClock& Clock() const { return m_clock; }
    SwarmRng& Rng() { return m_rng; }
    swarm::core::ITimerScheduler& Timers() { return m_timers; }
    /// The ports below are looked up once and cached (#46); the cache is
    /// refreshed when the stack is set, at DoInitialize, when an interface
    /// comes up and when an object is aggregated to the node.
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
    /// The same, through interface `ifIdx` (InterfaceTowards(next)).
    Ptr<Ipv4Route> DirectRoute(Ipv4Address dst, Ipv4Address next, Ipv4Address src,
                               int32_t ifIdx) const;
    void RefreshPorts() const;
    int32_t InterfaceTowards(Ipv4Address next) const;
    void PurgeTick();
    void RecvSocket(Ptr<Socket> socket);
    void WifiLinkFailed(Ipv4Address neighbor, Mac48Address mac);
    void WifiUndelivered(Ptr<const Packet> datagram, Ipv4Address neighbor);
    void IslLinkChanged(Ipv4Address peer, bool up);
    /// OnLinkUp for the peer of every interface whose beacons are suppressed.
    void AnnounceLinkStatePeers();
    /// The common teardown of Shutdown and Crash.
    void GoDown(SwarmLifecycleOp op);

    /// Counts every event and fires the matching trace source.
    class TracingObserver : public swarm::core::IRouterObserver {
    public:
        explicit TracingObserver(SwarmRoutingBase& base);
        void onControlSent(uint8_t kind, bool broadcast, uint32_t bytes) override;
        void onControlReceived(uint8_t kind, uint32_t bytes) override;
        void onRouteChanged(swarm::core::NodeAddress dest, swarm::core::NodeAddress nextHop,
                            bool added) override;
        void onDataDecision(swarm::core::NodeAddress dest,
                            swarm::core::RouteAction action) override;
        void onStateSize(uint32_t entries, uint32_t bytes) override;

        swarm::core::CountingObserver counting;

    private:
        SwarmRoutingBase& m_base;
    };

    Ptr<Ipv4> m_ipv4;
    Ptr<NetDevice> m_lo;
    /// Sockets in creation (interface) order: a map keyed by Ptr<Socket> would
    /// order them by heap address, and MainAddress(), the route fallbacks and
    /// the broadcast order would then vary between runs (org rule 5).
    /// A control socket, its interface address and that interface's index
    /// (kept, so no per-packet lookup is needed, #46).
    struct BoundSocket {
        Ptr<Socket> socket;
        Ipv4InterfaceAddress iface;
        int32_t ifIndex;
    };
    using SocketList = std::vector<BoundSocket>;
    SocketList m_sockets;
    SocketList m_bcastSockets;
    Ptr<DeferredPacketQueue> m_queue;
    Time m_purgeInterval;
    Timer m_purgeTimer;
    SwarmClock m_clock;
    SwarmRng m_rng;
    SwarmTimerScheduler m_timers;
    TracedCallback<uint8_t, bool, uint32_t> m_controlTx;
    TracedCallback<uint8_t, uint32_t> m_controlRx;
    TracedCallback<Ipv4Address, Ipv4Address, bool> m_routeChanged;
    TracedCallback<Ipv4Address, swarm::core::RouteAction> m_dataDecision;
    TracedCallback<uint32_t, uint32_t> m_stateSize;
    TracingObserver m_observer;  ///< after m_clock, which it reads
    /// Set across a control SendTo, so RouteOutput answers with a one-hop
    /// route to the neighbour instead of asking the protocol.
    Ipv4Address m_directTo;
    /// Ipv4L3Protocol's forwarding callback, the same for every packet: kept
    /// from RouteInput so ResendData can send outside it.
    UnicastForwardCallback m_ucb;
    SwarmNodeState m_state = SwarmNodeState::Up;
    /// IslHelloSuppression (#4), and the time-0 announcement it schedules.
    bool m_islHelloSuppression = false;
    EventId m_announceEvent;
    TracedCallback<SwarmNodeState, SwarmLifecycleOp> m_lifecycle;
    /// The cached ports (raw: they live in the node's aggregate, which holds
    /// this protocol too; dropped at dispose).
    mutable bool m_portsStale = true;
    mutable const swarm::core::ILinkState* m_linkStatePort = nullptr;
    mutable const swarm::core::ILinkQuality* m_linkQualityPort = nullptr;
    mutable const swarm::core::INeighborProvider* m_neighborsPort = nullptr;
    mutable const swarm::core::IEnergy* m_energyPort = nullptr;
};

}  // namespace ns3

#endif  // SWARM_ROUTING_BASE_H
