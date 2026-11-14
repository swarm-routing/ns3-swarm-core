// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet
// ns3/baselines/adaptive/model/adaptive-routing-base.cc @ 3e53f04
// (GPL-2.0-only); see swarm-routing-base.h.
#include "swarm-routing-base.h"

#include "swarm-energy-metric-hook.h"
#include "swarm-isl-link-monitor.h"
#include "swarm-wifi-link-monitor.h"

#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-route.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-header.h"
#include "ns3/udp-socket-factory.h"

#include <map>
#include <string>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SwarmRoutingBase");

namespace {

/// IANA protocol number of UDP. Spelled out: the ns-3 constant moved between
/// releases (UdpL4Protocol::PROT_NUMBER is deprecated in 3.49).
constexpr uint8_t kUdpProtocol = 17;

/// Kind names by protocol TypeId name, then kind: ordered maps (org rule 5),
/// bounded by the registered protocols x 256 kinds (org rule 7).
std::map<std::string, std::map<uint8_t, std::string>>& KindRegistry() {
    static std::map<std::string, std::map<uint8_t, std::string>> registry;
    return registry;
}

}  // namespace

// --- SwarmPrevHopTag -----------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED(SwarmPrevHopTag);

TypeId SwarmPrevHopTag::GetTypeId() {
    static TypeId tid = TypeId("ns3::SwarmPrevHopTag")
                            .SetParent<Tag>()
                            .SetGroupName("SwarmCore")
                            .AddConstructor<SwarmPrevHopTag>();
    return tid;
}

TypeId SwarmPrevHopTag::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t SwarmPrevHopTag::GetSerializedSize() const {
    return 8;
}

void SwarmPrevHopTag::Serialize(TagBuffer i) const {
    i.WriteU32(m_sender.Get());
    i.WriteU32(m_from.Get());
}

void SwarmPrevHopTag::Deserialize(TagBuffer i) {
    m_sender = Ipv4Address(i.ReadU32());
    m_from = Ipv4Address(i.ReadU32());
}

void SwarmPrevHopTag::Print(std::ostream& os) const {
    os << "prevHop=" << m_sender << " from=" << m_from;
}

// --- SwarmRoutingBase ------------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED(SwarmRoutingBase);

TypeId SwarmRoutingBase::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::SwarmRoutingBase")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("SwarmCore")
            .AddAttribute("DeferredQueue",
                          "The queue holding data packets while a route is looked for "
                          "(its MaxPackets and MaxAge attributes bound it).",
                          PointerValue(), MakePointerAccessor(&SwarmRoutingBase::m_queue),
                          MakePointerChecker<DeferredPacketQueue>())
            .AddAttribute("PurgeInterval",
                          "How often held packets older than the queue's MaxAge are "
                          "dropped (500 ms, AntHocNet's adaptive RoutingBase).",
                          TimeValue(MilliSeconds(500)),
                          MakeTimeAccessor(&SwarmRoutingBase::m_purgeInterval),
                          MakeTimeChecker(MilliSeconds(1)))
            .AddAttribute("IslHelloSuppression",
                          "Learn the neighbour of every point-to-point interface the node's "
                          "IslLinkMonitor calls authoritative from link state: beacons "
                          "(BroadcastBeacon) skip it, its peer is announced through OnLinkUp "
                          "at start and restart, and IsLinkStateNeighbor names it. Shared "
                          "media keep their beacons. Off by default (#4).",
                          BooleanValue(false),
                          MakeBooleanAccessor(&SwarmRoutingBase::m_islHelloSuppression),
                          MakeBooleanChecker())
            .AddTraceSource("ControlTx",
                            "A control message left this node: its kind (the protocol's wire "
                            "type byte), whether it was broadcast, and its size in bytes.",
                            MakeTraceSourceAccessor(&SwarmRoutingBase::m_controlTx),
                            "ns3::SwarmRoutingBase::ControlTxTracedCallback")
            .AddTraceSource("ControlRx",
                            "This node processed a received control message: its kind and size.",
                            MakeTraceSourceAccessor(&SwarmRoutingBase::m_controlRx),
                            "ns3::SwarmRoutingBase::ControlRxTracedCallback")
            .AddTraceSource("RouteChanged",
                            "A route to a destination through a next hop was added (true) or "
                            "removed (false).",
                            MakeTraceSourceAccessor(&SwarmRoutingBase::m_routeChanged),
                            "ns3::SwarmRoutingBase::RouteChangedTracedCallback")
            .AddTraceSource("DataDecision",
                            "What the protocol decided for a data packet: forwarded (Unicast), "
                            "Queue, Drop or Deliver.",
                            MakeTraceSourceAccessor(&SwarmRoutingBase::m_dataDecision),
                            "ns3::SwarmRoutingBase::DataDecisionTracedCallback")
            .AddTraceSource("Lifecycle",
                            "The node changed lifecycle state (Up or Down) by an operation "
                            "(Start, Shutdown or Crash).",
                            MakeTraceSourceAccessor(&SwarmRoutingBase::m_lifecycle),
                            "ns3::SwarmRoutingBase::LifecycleTracedCallback")
            .AddTraceSource("StateSize",
                            "The protocol's routing-state footprint: entries and bytes.",
                            MakeTraceSourceAccessor(&SwarmRoutingBase::m_stateSize),
                            "ns3::SwarmRoutingBase::StateSizeTracedCallback");
    return tid;
}

SwarmRoutingBase::SwarmRoutingBase()
    : m_queue(CreateObject<DeferredPacketQueue>()), m_purgeInterval(MilliSeconds(500)),
      m_purgeTimer(Timer::CANCEL_ON_DESTROY), m_observer(*this) {}

SwarmRoutingBase::~SwarmRoutingBase() = default;

int64_t SwarmRoutingBase::AssignStreams(int64_t stream) {
    return m_rng.AssignStreams(stream);
}

void SwarmRoutingBase::DoInitialize() {
    m_portsStale = true;
    m_purgeTimer.SetFunction(&SwarmRoutingBase::PurgeTick, this);
    m_purgeTimer.Schedule(m_purgeInterval);
    if (Ptr<Node> node = NodeOf()) {
        if (Ptr<WifiLinkMonitor> wifi = node->GetObject<WifiLinkMonitor>()) {
            wifi->TraceConnectWithoutContext("LinkFailure",
                                             MakeCallback(&SwarmRoutingBase::WifiLinkFailed, this));
            wifi->TraceConnectWithoutContext(
                "Undelivered", MakeCallback(&SwarmRoutingBase::WifiUndelivered, this));
        }
    }
    m_initialized = true;
    ConnectIslMonitor();
    Ipv4RoutingProtocol::DoInitialize();
}

void SwarmRoutingBase::DoDispose() {
    m_timers.CancelAll();
    m_announceEvent.Cancel();
    for (auto& kv : m_sockets) {
        kv.socket->Close();
    }
    for (auto& kv : m_bcastSockets) {
        kv.socket->Close();
    }
    m_sockets.clear();
    m_bcastSockets.clear();
    m_purgeTimer.Cancel();
    if (m_queue) {
        m_queue->Dispose();
        m_queue = nullptr;
    }
    m_ipv4 = nullptr;
    m_lo = nullptr;
    m_portsStale = true;  // the cached ports point into the node's aggregate
    Ipv4RoutingProtocol::DoDispose();
}

void SwarmRoutingBase::SetIpv4(Ptr<Ipv4> ipv4) {
    NS_ASSERT(ipv4);
    NS_ASSERT(!m_ipv4);
    m_ipv4 = ipv4;
    // Interface 0 is the loopback device, added before any routing protocol.
    m_lo = m_ipv4->GetNetDevice(0);
    m_portsStale = true;
}

Ptr<Node> SwarmRoutingBase::NodeOf() const {
    // The Ipv4 stack is aggregated to its node; the protocol itself only when
    // its helper aggregates it, so ask the stack.
    return m_ipv4 ? m_ipv4->GetObject<Node>() : nullptr;
}

// --- ports ---------------------------------------------------------------------

void SwarmRoutingBase::RefreshPorts() const {
    static const NullLinkState kNullLinkState;
    static const NullLinkQuality kNullLinkQuality;
    static const NullNeighborProvider kNullNeighbors;
    static const NullEnergy kNullEnergy;
    Ptr<Node> node = NodeOf();
    Ptr<WifiLinkMonitor> wifi = node ? node->GetObject<WifiLinkMonitor>() : nullptr;
    Ptr<IslLinkMonitor> isl = node ? node->GetObject<IslLinkMonitor>() : nullptr;
    Ptr<EnergyMetricHook> hook = node ? node->GetObject<EnergyMetricHook>() : nullptr;
    m_linkStatePort = wifi  ? static_cast<const swarm::core::ILinkState*>(PeekPointer(wifi))
                      : isl ? static_cast<const swarm::core::ILinkState*>(PeekPointer(isl))
                            : &kNullLinkState;
    m_linkQualityPort =
        wifi ? static_cast<const swarm::core::ILinkQuality*>(PeekPointer(wifi)) : &kNullLinkQuality;
    m_neighborsPort = isl ? static_cast<const swarm::core::INeighborProvider*>(PeekPointer(isl))
                          : &kNullNeighbors;
    m_energyPort =
        hook ? static_cast<const swarm::core::IEnergy*>(PeekPointer(hook)) : &kNullEnergy;
    // Without a node yet, look again next time: SetIpv4 has not run.
    m_portsStale = !node;
}

void SwarmRoutingBase::NotifyNewAggregate() {
    // A monitor aggregated to the node after the ports were read (#46).
    m_portsStale = true;
    if (m_initialized) {
        ConnectIslMonitor();  // one aggregated after initialisation (#4)
    }
    Ipv4RoutingProtocol::NotifyNewAggregate();
}

const swarm::core::ILinkState& SwarmRoutingBase::LinkState() const {
    if (m_portsStale) {
        RefreshPorts();
    }
    return *m_linkStatePort;
}

const swarm::core::ILinkQuality& SwarmRoutingBase::LinkQuality() const {
    if (m_portsStale) {
        RefreshPorts();
    }
    return *m_linkQualityPort;
}

const swarm::core::INeighborProvider& SwarmRoutingBase::Neighbors() const {
    if (m_portsStale) {
        RefreshPorts();
    }
    return *m_neighborsPort;
}

const swarm::core::IEnergy& SwarmRoutingBase::Energy() const {
    if (m_portsStale) {
        RefreshPorts();
    }
    return *m_energyPort;
}

// --- the router observer ------------------------------------------------------------

SwarmRoutingBase::TracingObserver::TracingObserver(SwarmRoutingBase& base)
    : counting(base.m_clock), m_base(base) {}

void SwarmRoutingBase::TracingObserver::onControlSent(uint8_t kind, bool broadcast,
                                                      uint32_t bytes) {
    counting.onControlSent(kind, broadcast, bytes);
    m_base.m_controlTx(kind, broadcast, bytes);
}

void SwarmRoutingBase::TracingObserver::onControlReceived(uint8_t kind, uint32_t bytes) {
    counting.onControlReceived(kind, bytes);
    m_base.m_controlRx(kind, bytes);
}

void SwarmRoutingBase::TracingObserver::onRouteChanged(swarm::core::NodeAddress dest,
                                                       swarm::core::NodeAddress nextHop,
                                                       bool added) {
    counting.onRouteChanged(dest, nextHop, added);
    m_base.m_routeChanged(ToIpv4Address(dest), ToIpv4Address(nextHop), added);
}

void SwarmRoutingBase::TracingObserver::onDataDecision(swarm::core::NodeAddress dest,
                                                       swarm::core::RouteAction action) {
    counting.onDataDecision(dest, action);
    m_base.m_dataDecision(ToIpv4Address(dest), action);
}

void SwarmRoutingBase::TracingObserver::onStateSize(uint32_t entries, uint32_t bytes) {
    counting.onStateSize(entries, bytes);
    m_base.m_stateSize(entries, bytes);
}

swarm::core::RouterStats SwarmRoutingBase::GetRouterStats() const {
    return m_observer.counting.stats();
}

void SwarmRoutingBase::ResetRouterStats() {
    m_observer.counting.reset();
}

void SwarmRoutingBase::SetKindNames(TypeId protocol, std::span<const swarm::core::KindName> names) {
    std::map<uint8_t, std::string>& table = KindRegistry()[protocol.GetName()];
    table.clear();
    for (const swarm::core::KindName& k : names) {
        if (k.name) {
            table[k.kind] = k.name;
        }
    }
}

std::string SwarmRoutingBase::GetKindName(TypeId protocol, uint8_t kind) {
    const auto& registry = KindRegistry();
    for (TypeId t = protocol;; t = t.GetParent()) {
        auto it = registry.find(t.GetName());
        if (it != registry.end()) {
            auto k = it->second.find(kind);
            if (k != it->second.end()) {
                return k->second;
            }
        }
        if (!t.HasParent() || t.GetParent() == t) {
            break;
        }
    }
    return std::to_string(kind);
}

std::string SwarmRoutingBase::KindName(uint8_t kind) const {
    return GetKindName(GetInstanceTypeId(), kind);
}

// --- addresses and routes --------------------------------------------------------

Ipv4Address SwarmRoutingBase::MainAddress() const {
    return m_sockets.empty() ? Ipv4Address() : m_sockets.begin()->iface.GetLocal();
}

bool SwarmRoutingBase::IsMyAddress(Ipv4Address a) const {
    for (const auto& kv : m_sockets) {
        if (kv.iface.GetLocal() == a) {
            return true;
        }
    }
    return false;
}

int32_t SwarmRoutingBase::InterfaceTowards(Ipv4Address next) const {
    for (const auto& kv : m_sockets) {
        const Ipv4InterfaceAddress& ifa = kv.iface;
        if (next.CombineMask(ifa.GetMask()) == ifa.GetLocal().CombineMask(ifa.GetMask())) {
            return kv.ifIndex;
        }
    }
    if (!m_sockets.empty()) {
        return m_sockets.begin()->ifIndex;
    }
    return -1;
}

Ptr<Ipv4Route> SwarmRoutingBase::DirectRoute(Ipv4Address dst, Ipv4Address next,
                                             Ipv4Address src) const {
    return DirectRoute(dst, next, src, InterfaceTowards(next));
}

Ptr<Ipv4Route> SwarmRoutingBase::DirectRoute(Ipv4Address dst, Ipv4Address next, Ipv4Address src,
                                             int32_t ifIdx) const {
    if (ifIdx < 0) {
        return nullptr;
    }
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(dst);
    route->SetGateway(next);
    route->SetSource(src == Ipv4Address() || src.IsAny() ? m_ipv4->GetAddress(ifIdx, 0).GetLocal()
                                                         : src);
    route->SetOutputDevice(m_ipv4->GetNetDevice(ifIdx));
    return route;
}

Ptr<Ipv4Route> SwarmRoutingBase::LoopbackRoute(const Ipv4Header& header, Ptr<NetDevice> oif) const {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(header.GetDestination());
    route->SetGateway(Ipv4Address("127.0.0.1"));
    Ipv4Address src;
    for (const auto& kv : m_sockets) {
        if (!oif || m_ipv4->GetInterfaceForDevice(oif) == kv.ifIndex) {
            src = kv.iface.GetLocal();
            break;
        }
    }
    if (src == Ipv4Address() && !m_sockets.empty()) {
        src = m_sockets.begin()->iface.GetLocal();
    }
    route->SetSource(src);
    route->SetOutputDevice(m_lo);
    return route;
}

Ptr<Ipv4Route> SwarmRoutingBase::RouteOutput(Ptr<Packet> p, const Ipv4Header& header,
                                             Ptr<NetDevice> oif, Socket::SocketErrno& sockerr) {
    sockerr = Socket::ERROR_NOTERROR;
    if (m_sockets.empty() || IsDown()) {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }
    const Ipv4Address dst = header.GetDestination();
    if (m_directTo != Ipv4Address() && dst == m_directTo) {
        return DirectRoute(dst, dst, header.GetSource());  // a UnicastControl
    }
    const Ipv4Address next = SelectNextHop(dst, MainAddress(), Ipv4Address(), p);
    if (next != Ipv4Address()) {
        if (Ptr<Ipv4Route> route = DirectRoute(dst, next, header.GetSource())) {
            if (p) {
                SwarmPrevHopTag old;
                p->RemovePacketTag(old);
                p->AddPacketTag(SwarmPrevHopTag(route->GetSource()));
            }
            OnDataForwarded(p, header, next);
            return route;
        }
    }
    // No next hop yet: bounce through loopback so RouteInput can hold the
    // packet, with its callbacks, while the protocol looks for a route.
    return LoopbackRoute(header, oif);
}

bool SwarmRoutingBase::RouteInput(Ptr<const Packet> p, const Ipv4Header& header,
                                  Ptr<const NetDevice> idev, const UnicastForwardCallback& ucb,
                                  const MulticastForwardCallback&, const LocalDeliverCallback& lcb,
                                  const ErrorCallback& ecb) {
    if (m_sockets.empty() || IsDown()) {
        return false;  // a down node routes nothing (#43)
    }
    if (m_ucb.IsNull() && !ucb.IsNull()) {
        m_ucb = ucb;
    }
    const Ipv4Address dst = header.GetDestination();
    const int32_t iif = m_ipv4->GetInterfaceForDevice(idev);
    bool control = false;
    if (header.GetProtocol() == kUdpProtocol) {
        UdpHeader udp;
        if (p->PeekHeader(udp) && udp.GetDestinationPort() == ControlPort()) {
            control = true;
        }
    }
    SwarmPrevHopTag tag;
    const Ipv4Address prev = p->PeekPacketTag(tag) ? tag.Get() : Ipv4Address();

    if (m_ipv4->IsDestinationAddress(dst, iif)) {
        if (!control && idev != m_lo && prev != Ipv4Address() && !OnDataReceived(p, header, prev)) {
            return true;
        }
        if (!lcb.IsNull()) {
            lcb(p, header, iif);
            return true;
        }
        return false;
    }
    if (dst.IsMulticast() || dst.IsBroadcast()) {
        return false;
    }
    if (idev == m_lo) {
        // A locally originated packet RouteOutput had no route for. It leaves
        // through ucb -> IpForward, which decrements the TTL for a hop no
        // radio carried: add it back (ns-3 AODV does the same).
        Ipv4Header compensated = header;
        compensated.SetTtl(header.GetTtl() + 1);
        Defer(p, compensated, ucb, ecb);
        return true;
    }
    if (control) {
        return false;  // control messages are one-hop only, never forwarded
    }
    if (!OnDataReceived(p, header, prev)) {
        return true;
    }
    const Ipv4Address next = SelectNextHop(dst, header.GetSource(), prev, p);
    if (next != Ipv4Address()) {
        OnDataForwarded(p, header, next);
        ForwardData(p, header, next, ucb);
        return true;
    }
    Defer(p, header, ucb, ecb);
    return true;
}

void SwarmRoutingBase::ForwardData(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address next,
                                   const UnicastForwardCallback& ucb) {
    const int32_t ifIdx = InterfaceTowards(next);  // once per forward (#46)
    Ptr<Ipv4Route> route = DirectRoute(header.GetDestination(), next, header.GetSource(), ifIdx);
    if (!route || ucb.IsNull()) {
        return;
    }
    Ptr<Packet> q = p->Copy();
    // `from`: where this node had the packet from. A packet the MAC gave back
    // (ResendData) still carries this node's own tag: keep its `from`.
    SwarmPrevHopTag old;
    Ipv4Address from;
    if (q->RemovePacketTag(old)) {
        from = IsMyAddress(old.Get()) ? old.GetFrom() : old.Get();
    }
    q->AddPacketTag(SwarmPrevHopTag(m_ipv4->GetAddress(ifIdx, 0).GetLocal(), from));
    ucb(route, q, header);
}

bool SwarmRoutingBase::ResendData(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address next) {
    if (IsDown() || m_ucb.IsNull() || InterfaceTowards(next) < 0) {
        return false;
    }
    ForwardData(p, header, next, m_ucb);
    return true;
}

// --- the deferred queue ------------------------------------------------------------

void SwarmRoutingBase::Defer(Ptr<const Packet> p, const Ipv4Header& header,
                             const UnicastForwardCallback& ucb, const ErrorCallback& ecb) {
    Defer(p, header, ucb, ecb, HoldClassFor(header.GetDestination()));
}

void SwarmRoutingBase::Defer(Ptr<const Packet> p, const Ipv4Header& header,
                             const UnicastForwardCallback& ucb, const ErrorCallback& ecb,
                             uint8_t holdClass) {
    DeferredPacket e;
    e.packet = p;
    e.header = header;
    e.ucb = ucb;
    e.ecb = ecb;
    e.holdClass = holdClass;
    m_queue->Enqueue(e);
    if (IsDown()) {
        m_queue->Flush();  // an adapter deferring while down: lost as NodeDown
        return;
    }
    StartDiscovery(header.GetDestination());
}

// --- node lifecycle ----------------------------------------------------------------

void SwarmRoutingBase::Shutdown() {
    if (IsDown()) {
        return;
    }
    DoNodeShutdown();  // still up: a leaving control goes out
    GoDown(SwarmLifecycleOp::Shutdown);
}

void SwarmRoutingBase::Crash() {
    if (IsDown()) {
        return;
    }
    m_state = SwarmNodeState::Down;  // first: nothing the hook sends leaves
    DoNodeCrash();
    GoDown(SwarmLifecycleOp::Crash);
}

void SwarmRoutingBase::GoDown(SwarmLifecycleOp op) {
    m_state = SwarmNodeState::Down;
    m_timers.CancelAll();
    m_announceEvent.Cancel();
    m_purgeTimer.Cancel();
    if (m_queue) {
        m_queue->Flush();
    }
    m_lifecycle(m_state, op);
}

void SwarmRoutingBase::Start() {
    if (!IsDown()) {
        return;
    }
    m_state = SwarmNodeState::Up;
    if (m_purgeTimer.IsRunning()) {
        m_purgeTimer.Cancel();
    }
    m_purgeTimer.SetFunction(&SwarmRoutingBase::PurgeTick, this);
    m_purgeTimer.Schedule(m_purgeInterval);
    DoNodeStart();
    if (m_islHelloSuppression) {
        AnnounceLinkStatePeers();  // no hello will teach the protocol these (#4)
    }
    m_lifecycle(m_state, SwarmLifecycleOp::Start);
}

void SwarmRoutingBase::PurgeTick() {
    m_queue->Purge();
    m_purgeTimer.Schedule(m_purgeInterval);
}

void SwarmRoutingBase::ReleaseDeferred(Ipv4Address dst) {
    std::vector<DeferredPacket> back;
    for (DeferredPacket& e : m_queue->Release(dst)) {
        SwarmPrevHopTag tag;
        Ipv4Address prev;
        if (e.packet->PeekPacketTag(tag) && !IsMyAddress(tag.Get())) {
            prev = tag.Get();
        }
        const Ipv4Address next = SelectNextHop(dst, e.header.GetSource(), prev, e.packet);
        if (next == Ipv4Address()) {
            back.push_back(std::move(e));  // keeps its first enqueue time
            continue;
        }
        if (!e.ucb.IsNull() && InterfaceTowards(next) >= 0) {
            m_queue->NoteForwarded(e);  // only what ForwardData will send
        }
        OnDataForwarded(e.packet, e.header, next);
        ForwardData(e.packet, e.header, next, e.ucb);
    }
    if (!back.empty()) {
        m_queue->Requeue(std::move(back));  // once, in order (#46)
    }
}

void SwarmRoutingBase::DiscardDeferred(Ipv4Address dst) {
    m_queue->Discard(dst);
}

// --- control sockets -----------------------------------------------------------------

void SwarmRoutingBase::NotifyInterfaceUp(uint32_t interface) {
    m_portsStale = true;
    Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
    if (Ptr<IslLinkMonitor> isl = NodeOf()->GetObject<IslLinkMonitor>()) {
        isl->NotifyInterface(interface, true);
    }
    if (l3->GetNAddresses(interface) == 0) {
        return;
    }
    const Ipv4InterfaceAddress iface = l3->GetAddress(interface, 0);
    if (iface.GetLocal() == Ipv4Address::GetLoopback()) {
        return;
    }
    Ptr<NetDevice> dev = l3->GetNetDevice(interface);
    for (const Ipv4Address bind : {iface.GetLocal(), iface.GetBroadcast()}) {
        Ptr<Socket> s = Socket::CreateSocket(NodeOf(), UdpSocketFactory::GetTypeId());
        s->SetRecvCallback(MakeCallback(&SwarmRoutingBase::RecvSocket, this));
        s->BindToNetDevice(dev);
        s->Bind(InetSocketAddress(bind, ControlPort()));
        s->SetAllowBroadcast(true);
        s->SetIpTtl(1);
        (bind == iface.GetLocal() ? m_sockets : m_bcastSockets)
            .push_back({s, iface, static_cast<int32_t>(interface)});
    }
}

void SwarmRoutingBase::NotifyInterfaceDown(uint32_t interface) {
    if (Ptr<IslLinkMonitor> isl = NodeOf()->GetObject<IslLinkMonitor>()) {
        isl->NotifyInterface(interface, false);
    }
    for (auto* m : {&m_sockets, &m_bcastSockets}) {
        for (auto it = m->begin(); it != m->end();) {
            if (it->ifIndex == static_cast<int32_t>(interface)) {
                it->socket->Close();
                it = m->erase(it);
            } else {
                ++it;
            }
        }
    }
}

void SwarmRoutingBase::NotifyAddAddress(uint32_t, Ipv4InterfaceAddress) {}

void SwarmRoutingBase::NotifyRemoveAddress(uint32_t, Ipv4InterfaceAddress) {}

void SwarmRoutingBase::BroadcastControl(Ptr<Packet> p) {
    if (IsDown()) {
        return;
    }
    for (const auto& kv : m_sockets) {
        kv.socket->SendTo(p->Copy(), 0, InetSocketAddress(kv.iface.GetBroadcast(), ControlPort()));
    }
}

uint32_t SwarmRoutingBase::BroadcastBeacon(Ptr<Packet> p) {
    if (IsDown()) {
        return 0;
    }
    uint32_t sent = 0;
    for (const auto& kv : m_sockets) {
        if (BeaconSuppressed(static_cast<uint32_t>(kv.ifIndex))) {
            continue;
        }
        kv.socket->SendTo(p->Copy(), 0, InetSocketAddress(kv.iface.GetBroadcast(), ControlPort()));
        ++sent;
    }
    return sent;
}

bool SwarmRoutingBase::BeaconSuppressed(uint32_t interface) const {
    if (!m_islHelloSuppression) {
        return false;
    }
    Ptr<Node> node = NodeOf();
    Ptr<IslLinkMonitor> isl = node ? node->GetObject<IslLinkMonitor>() : nullptr;
    return isl && isl->IsAuthoritative(interface);
}

bool SwarmRoutingBase::IsLinkStateNeighbor(Ipv4Address neighbor) const {
    if (!m_islHelloSuppression || neighbor == Ipv4Address()) {
        return false;
    }
    Ptr<Node> node = NodeOf();
    Ptr<IslLinkMonitor> isl = node ? node->GetObject<IslLinkMonitor>() : nullptr;
    if (!isl) {
        return false;
    }
    for (const auto& kv : m_sockets) {
        const auto iface = static_cast<uint32_t>(kv.ifIndex);
        if (isl->IsAuthoritative(iface) && isl->PeerOnInterface(iface) == neighbor) {
            return true;
        }
    }
    return false;
}

void SwarmRoutingBase::ConnectIslMonitor() {
    Ptr<Node> node = NodeOf();
    Ptr<IslLinkMonitor> isl = node ? node->GetObject<IslLinkMonitor>() : nullptr;
    if (m_islConnected || !isl) {
        return;
    }
    m_islConnected = true;
    isl->TraceConnectWithoutContext("LinkChange",
                                    MakeCallback(&SwarmRoutingBase::IslLinkChanged, this));
    if (m_islHelloSuppression) {
        // The far end's carrier: only a node that trusts link state needs it,
        // so the off path sees exactly the events it saw before.
        isl->TraceConnectWithoutContext("PeerLinkChange",
                                        MakeCallback(&SwarmRoutingBase::IslLinkChanged, this));
        // An event, not a call: the adapter's own DoInitialize (which may
        // chain up before its core exists) has finished by then. Scheduled
        // only when on, so the off path is event-identical.
        m_announceEvent = Simulator::ScheduleNow(&SwarmRoutingBase::AnnounceLinkStatePeers, this);
    }
}

void SwarmRoutingBase::AnnounceLinkStatePeers() {
    Ptr<Node> node = NodeOf();
    Ptr<IslLinkMonitor> isl = node ? node->GetObject<IslLinkMonitor>() : nullptr;
    if (!isl || IsDown()) {
        return;
    }
    // Interface order (the socket list's), so the announcements are
    // deterministic (org rule 5).
    for (const auto& kv : m_sockets) {
        const auto iface = static_cast<uint32_t>(kv.ifIndex);
        if (isl->IsAuthoritative(iface)) {
            OnLinkUp(isl->PeerOnInterface(iface));
        }
    }
}

void SwarmRoutingBase::UnicastControl(Ptr<Packet> p, Ipv4Address neighbor) {
    if (IsDown()) {
        return;
    }
    const int32_t ifIdx = InterfaceTowards(neighbor);
    for (const auto& kv : m_sockets) {
        if (kv.ifIndex == ifIdx) {
            m_directTo = neighbor;
            kv.socket->SendTo(p, 0, InetSocketAddress(neighbor, ControlPort()));
            m_directTo = Ipv4Address();
            return;
        }
    }
}

void SwarmRoutingBase::RecvSocket(Ptr<Socket> socket) {
    Address from;
    Ptr<Packet> p = socket->RecvFrom(from);
    if (!p) {
        return;
    }
    const Ipv4Address sender = InetSocketAddress::ConvertFrom(from).GetIpv4();
    if (IsDown() || IsMyAddress(sender)) {
        return;
    }
    Ipv4Address receiver;
    for (const SocketList* list : {&m_sockets, &m_bcastSockets}) {
        for (const BoundSocket& b : *list) {
            if (b.socket == socket) {
                receiver = b.iface.GetLocal();
            }
        }
    }
    RecvControl(p, sender, receiver);
}

// --- link failures -----------------------------------------------------------------

void SwarmRoutingBase::WifiLinkFailed(Ipv4Address neighbor, Mac48Address) {
    if (neighbor != Ipv4Address() && !IsDown()) {
        OnLinkFailure(neighbor);
    }
}

void SwarmRoutingBase::WifiUndelivered(Ptr<const Packet> datagram, Ipv4Address neighbor) {
    if (IsDown()) {
        return;
    }
    Ptr<Packet> p = datagram->Copy();
    Ipv4Header header;
    p->RemoveHeader(header);
    if (Node::ChecksumEnabled()) {
        header.EnableChecksum();  // Deserialize does not restore it: a resend needs it
    }
    if (header.GetDestination().IsBroadcast() || header.GetDestination().IsMulticast()) {
        return;
    }
    // Only a first fragment starts with the UDP header.
    if (header.GetProtocol() == kUdpProtocol && header.GetFragmentOffset() == 0 &&
        p->GetSize() >= UdpHeader().GetSerializedSize()) {
        UdpHeader udp;
        if (p->PeekHeader(udp) && udp.GetDestinationPort() == ControlPort()) {
            return;  // control is one hop; its loss is the protocol's own business
        }
    }
    SwarmPrevHopTag tag;
    const Ipv4Address prev =
        p->PeekPacketTag(tag) && IsMyAddress(tag.Get()) ? tag.GetFrom() : Ipv4Address();
    OnDataUndelivered(p, header, neighbor, prev);
}

void SwarmRoutingBase::IslLinkChanged(Ipv4Address peer, bool up) {
    if (IsDown()) {
        return;  // a down node hears nothing; DoNodeStart re-learns its links
    }
    if (up) {
        OnLinkUp(peer);
    } else {
        OnLinkFailure(peer);
    }
}

void SwarmRoutingBase::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit) const {
    *stream->GetStream() << "swarm routing at " << MainAddress() << ", "
                         << (m_queue ? m_queue->Size() : 0) << " packet(s) deferred\n";
}

}  // namespace ns3
