// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Tests of the ns-3 glue: the ports over ns-3, DeferredPacketQueue,
// SwarmRoutingBase (through a toy protocol with no routing logic), each
// monitor in a two- or three-node scenario, the router observer (its trace
// sources, counters and kind names), neighbour discovery from link state on
// point-to-point links (#4), and determinism: the same seed gives
// byte-identical output with and without every monitor and observer attached.
#include "swarm-glue-tests.h"

#include "ns3/basic-energy-source-helper.h"
#include "ns3/config.h"
#include "ns3/global-value.h"
#include "ns3/boolean.h"
#include "ns3/buffer.h"
#include "ns3/double.h"
#include "ns3/energy-source-container.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/node-container.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/pointer.h"
#include "ns3/simple-device-energy-model.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/swarm-core-module.h"
#include "ns3/tag.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace ns3::swarmtest {

namespace {

constexpr uint16_t kDataPort = 9;

/// The toy protocol's control kinds, as a protocol names its own.
constexpr uint8_t kHello = 1;
constexpr uint8_t kMessage = 2;
constexpr std::array<swarm::core::KindName, 2> kToyKinds{{{kHello, "HELLO"}, {kMessage, "MSG"}}};

/// A mark an adapter puts on a packet (a test stand-in for a protocol header
/// that rides on data packets). ns-3's IP layer leaves an unknown tag alone.
class MarkTag : public Tag {
public:
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("ns3::swarmtest::MarkTag")
                                .SetParent<Tag>()
                                .SetGroupName("SwarmCore")
                                .AddConstructor<MarkTag>();
        return tid;
    }

    TypeId GetInstanceTypeId() const override { return GetTypeId(); }

    uint32_t GetSerializedSize() const override { return 0; }

    void Serialize(TagBuffer) const override {}

    void Deserialize(TagBuffer) override {}

    void Print(std::ostream& os) const override { os << "mark"; }
};

// --- a toy protocol: static next hops, learned after a "discovery" delay
// ------

/// A SwarmRoutingBase with no routing logic: next hops come from a table the
/// test fills; a route in `pending` is learned `discoveryDelay` (plus a jitter
/// drawn from the Rng port) after StartDiscovery asks for it. It records what
/// the base hands it, so the test can check the plumbing, and reports every
/// event through Observer() as a protocol's core would.
class ToyProtocol : public SwarmRoutingBase {
public:
    static TypeId GetTypeId();

    std::map<Ipv4Address, Ipv4Address> routes;
    std::map<Ipv4Address, Ipv4Address> pending;
    double discoveryDelay = 0.5;
    double discoveryJitter = 0.0;
    double helloInterval = 0.0;  ///< 0: no hellos
    /// Hellos go through BroadcastBeacon (liveness only, #4) instead of
    /// BroadcastControl, and are reported only when they left.
    bool beacon = false;
    std::vector<Ipv4Address> dataFrom;
    std::vector<std::string> controlLog;
    std::vector<Ipv4Address> failures;
    std::vector<Ipv4Address> ups;
    uint32_t discoveries = 0;
    /// A relay that holds every transit packet through Defer with a mark on
    /// it, as an adapter overriding RouteInput would (#40).
    bool holdMarked = false;
    /// OnTxFailure (#128): one line per call ("tx data <dst>", "tx control",
    /// "tx other"), then the base's default unless `txOnly`; the default's
    /// OnLinkFailure and OnDataUndelivered add "link" and "undelivered".
    std::vector<std::string> txLog;
    bool txOnly = false;
    /// OnLinkFailure crashes the node (#128's default must then skip the
    /// data hook).
    bool crashOnLinkFailure = false;
    std::vector<Mac48Address> txMacs;
    std::vector<bool> markedFrom;  ///< per data packet seen: did it carry the mark?
    /// OnDataUndelivered (#40): (failed next hop, previous hop) per report; with
    /// `backtrack`, the packet goes back to the previous hop (ARA §3.3).
    std::vector<std::pair<Ipv4Address, Ipv4Address>> undelivered;
    std::vector<bool> resent;
    bool backtrack = false;
    Ipv4Address noRouteFrom;  ///< no next hop for a packet from this neighbour

    /// Lifecycle (#43): what each hook saw, the routes DoNodeStart restores,
    /// and a core timer that a shutdown or crash must cancel.
    std::vector<std::string> lifecycleLog;
    std::map<Ipv4Address, Ipv4Address> staticRoutes;
    bool probeRan = false;
    static constexpr uint32_t kLeavingSize = 5;

    void ArmProbe(double delay) {
        Timers().schedule(delay, [this]() { probeRan = true; });
    }

    void SendHelloNow() {
        if (beacon) {
            if (BroadcastBeacon(Create<Packet>(4)) > 0) {
                Observer().onControlSent(kHello, true, 4);
            }
            return;
        }
        BroadcastControl(Create<Packet>(4));
        Observer().onControlSent(kHello, true, 4);
    }

    void UnicastMessage(Ipv4Address to, uint32_t size) {
        Execute(swarm::core::RouteDecision<uint32_t>::unicast(ToNodeAddress(to), size),
                [](uint32_t s) { return Create<Packet>(s); });
        Observer().onControlSent(kMessage, false, size);
    }

    void DiscardViaCore(Ipv4Address dst) {
        Execute(swarm::core::RouteDecision<uint32_t>::discardPending(ToNodeAddress(dst)),
                [](uint32_t s) { return Create<Packet>(s); });
    }

    using SwarmRoutingBase::BeaconSuppressed;
    using SwarmRoutingBase::Energy;
    using SwarmRoutingBase::IsLinkStateNeighbor;
    using SwarmRoutingBase::LinkQuality;
    using SwarmRoutingBase::LinkState;
    using SwarmRoutingBase::Neighbors;

protected:
    void DoInitialize() override {
        if (helloInterval > 0.0) {
            ScheduleHello();
        }
        SwarmRoutingBase::DoInitialize();
    }

    uint16_t ControlPort() const override { return 4242; }

    Ipv4Address SelectNextHop(Ipv4Address dst, Ipv4Address, Ipv4Address prev,
                              Ptr<const Packet>) override {
        if (noRouteFrom != Ipv4Address() && prev == noRouteFrom) {
            return Ipv4Address();
        }
        auto it = routes.find(dst);
        return it == routes.end() ? Ipv4Address() : it->second;
    }

    void StartDiscovery(Ipv4Address dst) override {
        ++discoveries;
        Observer().onDataDecision(ToNodeAddress(dst), swarm::core::RouteAction::Queue);
        auto it = pending.find(dst);
        if (it == pending.end()) {
            return;
        }
        const Ipv4Address next = it->second;
        const double delay = discoveryDelay + discoveryJitter * Rng().uniform();
        Timers().schedule(delay, [this, dst, next]() {
            routes[dst] = next;
            Observer().onRouteChanged(ToNodeAddress(dst), ToNodeAddress(next), true);
            Observer().onStateSize(static_cast<uint32_t>(routes.size()),
                                   static_cast<uint32_t>(routes.size() * 8));
            ReleaseDeferred(dst);
        });
    }

    void RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address receiver) override {
        std::ostringstream os;
        os << Clock().now() << " " << from << "->" << receiver << " " << p->GetSize();
        controlLog.push_back(os.str());
        Observer().onControlReceived(p->GetSize() == 4 ? kHello : kMessage, p->GetSize());
    }

    bool OnDataReceived(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address prev) override {
        dataFrom.push_back(prev);
        MarkTag mark;
        markedFrom.push_back(p->PeekPacketTag(mark));
        if (IsMyAddress(header.GetDestination())) {
            Observer().onDataDecision(ToNodeAddress(header.GetDestination()),
                                      swarm::core::RouteAction::Deliver);
        }
        return true;
    }

    void OnDataForwarded(Ptr<const Packet>, const Ipv4Header& header, Ipv4Address) override {
        Observer().onDataDecision(ToNodeAddress(header.GetDestination()),
                                  swarm::core::RouteAction::Unicast);
    }

    bool RouteInput(Ptr<const Packet> p, const Ipv4Header& header, Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb, const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb, const ErrorCallback& ecb) override {
        const Ipv4Address dst = header.GetDestination();
        if (holdMarked && !IsMyAddress(dst) && !dst.IsBroadcast() && !dst.IsMulticast()) {
            Ptr<Packet> q = p->Copy();
            q->AddPacketTag(MarkTag());
            Defer(q, header, ucb, ecb);
            return true;
        }
        return SwarmRoutingBase::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
    }

    void OnLinkFailure(Ipv4Address neighbor) override {
        failures.push_back(neighbor);
        txLog.emplace_back("link");
        if (crashOnLinkFailure) {
            Crash();
        }
    }

    void OnTxFailure(Ipv4Address neighbor, Mac48Address mac, Ptr<const Packet> p,
                     const Ipv4Header* header, bool control) override {
        std::ostringstream os;
        os << "tx " << (p ? "data" : control ? "control" : "other");
        if (header) {
            os << " " << header->GetDestination();
        }
        txLog.push_back(os.str());
        txMacs.push_back(mac);
        if (!txOnly) {
            SwarmRoutingBase::OnTxFailure(neighbor, mac, p, header, control);
        }
    }

    // A graceful shutdown announces it is leaving; a crash tries to as well,
    // and the base must let nothing out. Both forget the routes; a restart
    // brings back the static ones.
    void DoNodeShutdown() override {
        lifecycleLog.emplace_back("shutdown");
        BroadcastControl(Create<Packet>(kLeavingSize));
        routes.clear();
    }

    void DoNodeCrash() override {
        lifecycleLog.emplace_back("crash");
        BroadcastControl(Create<Packet>(kLeavingSize));
        routes.clear();
    }

    void DoNodeStart() override {
        lifecycleLog.emplace_back("start");
        routes = staticRoutes;
        if (helloInterval > 0.0) {
            ScheduleHello();  // the crash cancelled it
        }
    }

    void OnLinkUp(Ipv4Address neighbor) override { ups.push_back(neighbor); }

    void OnDataUndelivered(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address failedNext,
                           Ipv4Address prev) override {
        undelivered.emplace_back(failedNext, prev);
        txLog.emplace_back("undelivered");
        if (backtrack && prev != Ipv4Address()) {
            resent.push_back(ResendData(p, header, prev));
        }
    }

private:
    void ScheduleHello() {
        Timers().schedule(helloInterval * (0.5 + Rng().uniform()), [this]() {
            SendHelloNow();
            ScheduleHello();
        });
    }
};

NS_OBJECT_ENSURE_REGISTERED(ToyProtocol);

TypeId ToyProtocol::GetTypeId() {
    static TypeId tid = TypeId("ns3::swarmtest::ToyProtocol")
                            .SetParent<SwarmRoutingBase>()
                            .SetGroupName("SwarmCore")
                            .AddConstructor<ToyProtocol>();
    return tid;
}

Ptr<ToyProtocol> Toy(Ptr<Node> node) {
    return node->GetObject<ToyProtocol>();
}

Ipv4Address Addr(Ptr<Node> node, uint32_t iface) {
    return node->GetObject<Ipv4>()->GetAddress(iface, 0).GetLocal();
}

/// A UDP sink on kDataPort that logs every packet it receives.
Ptr<Socket> Sink(Ptr<Node> node, std::vector<std::string>* log) {
    Ptr<Socket> s = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
    s->Bind(InetSocketAddress(Ipv4Address::GetAny(), kDataPort));
    s->SetRecvCallback(MakeBoundCallback(
        +[](std::vector<std::string>* out, Ptr<Socket> sock) {
            Address from;
            while (Ptr<Packet> p = sock->RecvFrom(from)) {
                std::ostringstream os;
                os << Simulator::Now().GetNanoSeconds() << " "
                   << InetSocketAddress::ConvertFrom(from).GetIpv4() << " " << p->GetSize();
                out->push_back(os.str());
            }
        },
        log));
    return s;
}

void SendUdp(Ptr<Socket> s, Ipv4Address to, uint32_t size) {
    s->SendTo(Create<Packet>(size), 0, InetSocketAddress(to, kDataPort));
}

Ptr<Socket> Source(Ptr<Node> node) {
    return Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
}

uint32_t g_errors = 0;

void CountError(Ptr<const Packet>, const Ipv4Header&, Socket::SocketErrno) {
    ++g_errors;
}

/// Three nodes in a point-to-point line, n0 - n1 - n2 (10.1.1.0/24,
/// 10.1.2.0/24), with the toy protocol and every monitor installed in one call.
struct Line {
    NodeContainer nodes;
    NetDeviceContainer d01;
    NetDeviceContainer d12;

    explicit Line(const std::string& rate01, bool islHelloSuppression = false) {
        nodes.Create(3);
        PointToPointHelper p2p;
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        p2p.SetDeviceAttribute("DataRate", StringValue(rate01));
        d01 = p2p.Install(nodes.Get(0), nodes.Get(1));
        p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
        d12 = p2p.Install(nodes.Get(1), nodes.Get(2));
        SwarmRoutingHelper helper("ns3::swarmtest::ToyProtocol");
        if (islHelloSuppression) {
            helper.Set("IslHelloSuppression", BooleanValue(true));
        }
        helper.Install(nodes);
        helper.AssignStreams(nodes, 200);  // runs in one process compare (org rule 5)
        Ipv4AddressHelper a;
        a.SetBase("10.1.1.0", "255.255.255.0");
        a.Assign(d01);
        a.SetBase("10.1.2.0", "255.255.255.0");
        a.Assign(d12);
    }
};

// --- the ports
// ------------------------------------------------------------------

class PortsTestCase : public TestCase {
public:
    PortsTestCase() : TestCase("glue: ports over ns-3 (clock, rng, timers, addresses)") {}

private:
    void DoRun() override {
        const Ipv4Address a("10.1.2.3");
        NS_TEST_ASSERT_MSG_EQ(ToIpv4Address(ToNodeAddress(a)), a, "address round trip");
        NS_TEST_ASSERT_MSG_EQ(ToNodeAddress(Ipv4Address()), swarm::core::kInvalidAddress,
                              "0.0.0.0 is no node");
        NS_TEST_ASSERT_MSG_EQ(ToIpv4Address(swarm::core::kInvalidAddress), Ipv4Address(),
                              "kInvalidAddress maps back to 0.0.0.0");

        SwarmClock clock;
        SwarmTimerScheduler timers;
        double firedAt = -1.0;
        bool cancelledRan = false;
        double clockAt = -1.0;
        timers.schedule(0.25, [&]() { firedAt = clock.now(); });
        timers.schedule(0.5, [&]() { cancelledRan = true; });
        Simulator::Schedule(Seconds(0.3), [&]() { timers.CancelAll(); });
        timers.schedule(-1.0, [&]() { clockAt = clock.now(); });  // negative: now
        Simulator::Run();
        Simulator::Destroy();
        NS_TEST_EXPECT_MSG_EQ_TOL(firedAt, 0.25, 1e-12, "timer fires at its delay");
        NS_TEST_EXPECT_MSG_EQ(cancelledRan, false, "CancelAll drops pending callbacks");
        NS_TEST_EXPECT_MSG_EQ_TOL(clockAt, 0.0, 1e-12, "a negative delay runs now");

        SwarmCoreSeed(3);
        SwarmRng r1;
        SwarmRng r2;
        NS_TEST_ASSERT_MSG_EQ(r1.AssignStreams(5), 1, "one stream");
        r2.AssignStreams(5);
        for (int i = 0; i < 5; ++i) {
            NS_TEST_EXPECT_MSG_EQ(r1.uniform(), r2.uniform(), "same stream, same draws");
            const int k = r1.uniformInt(4);
            r2.uniformInt(4);
            NS_TEST_EXPECT_MSG_EQ((k >= 0 && k < 4), true, "uniformInt in [0, n)");
        }
        NS_TEST_EXPECT_MSG_EQ(r1.uniformInt(0), 0, "n <= 0 never wraps");
        NullLinkState ls;
        NullLinkQuality lq;
        NullEnergy en;
        NullNeighborProvider nb;
        NS_TEST_EXPECT_MSG_EQ(ls.macQueueLength(1), 0, "null link state");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(lq.sinrDbFrom(1)), true, "null SINR is NaN");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(en.residualEnergyFraction()), true, "null energy NaN");
        NS_TEST_EXPECT_MSG_EQ(nb.neighbors().empty(), true, "no neighbours");
    }
};

// --- DeferredPacketQueue
// -----------------------------------------------------------

class DeferredQueueTestCase : public TestCase {
public:
    DeferredQueueTestCase()
        : TestCase("glue: DeferredPacketQueue bounded by size and age, drops "
                   "accounted") {}

private:
    static DeferredPacket Entry(Ipv4Address dst) {
        DeferredPacket e;
        e.packet = Create<Packet>(10);
        e.header.SetDestination(dst);
        e.ecb = MakeCallback(&CountError);
        return e;
    }

    void DoRun() override {
        Ptr<DeferredPacketQueue> q = CreateObject<DeferredPacketQueue>();
        UintegerValue maxPackets;
        TimeValue maxAge;
        q->GetAttribute("MaxPackets", maxPackets);
        q->GetAttribute("MaxAge", maxAge);
        NS_TEST_ASSERT_MSG_EQ(maxPackets.Get(), 64u, "documented default MaxPackets");
        NS_TEST_ASSERT_MSG_EQ(maxAge.Get(), Seconds(30), "documented default MaxAge");
        q->SetAttribute("MaxPackets", UintegerValue(3));
        q->SetAttribute("MaxAge", TimeValue(Seconds(1)));

        std::map<DeferredDropReason, int> traced;
        q->TraceConnectWithoutContext(
            "Drop", MakeBoundCallback(
                        +[](std::map<DeferredDropReason, int>* m, Ptr<const Packet>,
                            const Ipv4Header&, DeferredDropReason r) { ++(*m)[r]; },
                        &traced));
        g_errors = 0;
        const Ipv4Address a("10.0.0.1");
        const Ipv4Address b("10.0.0.2");
        const Ipv4Address c("10.0.0.3");
        size_t releasedA = 0;
        bool hasBBeforePurge = false;
        for (int i = 0; i < 4; ++i) {
            q->Enqueue(Entry(a));  // the 4th pushes the 1st out
        }
        Simulator::Schedule(Seconds(0.4), [&]() { releasedA = q->Release(a).size(); });
        Simulator::Schedule(Seconds(0.5), [&]() { q->Enqueue(Entry(b)); });
        Simulator::Schedule(Seconds(1.2), [&]() { hasBBeforePurge = q->Has(b); });
        Simulator::Schedule(Seconds(1.6), [&]() {
            q->Purge();  // b is 1.1 s old
            q->Enqueue(Entry(c));
            q->Discard(c);
        });
        Simulator::Run();
        Simulator::Destroy();

        const DeferredQueueStats& s = q->Stats();
        NS_TEST_EXPECT_MSG_EQ(releasedA, 3u, "the three newest packets to a are released");
        NS_TEST_EXPECT_MSG_EQ(hasBBeforePurge, true, "b still waits at 0.7 s old");
        NS_TEST_EXPECT_MSG_EQ(s.enqueued, 6u, "enqueued");
        NS_TEST_EXPECT_MSG_EQ(s.released, 3u, "released");
        NS_TEST_EXPECT_MSG_EQ(s.overflow, 1u, "overflow (MaxPackets)");
        NS_TEST_EXPECT_MSG_EQ(s.expired, 1u, "expired (MaxAge)");
        NS_TEST_EXPECT_MSG_EQ(s.discarded, 1u, "discarded");
        NS_TEST_EXPECT_MSG_EQ(s.enqueued,
                              s.released + s.overflow + s.expired + s.discarded + q->Size(),
                              "the counts add up");
        NS_TEST_EXPECT_MSG_EQ_TOL(s.releasedHoldSumS, 1.2, 1e-9, "hold time of the released");
        NS_TEST_EXPECT_MSG_EQ_TOL(s.releasedHoldMaxS, 0.4, 1e-9, "longest hold");
        NS_TEST_EXPECT_MSG_EQ(g_errors, 3u, "every drop goes through the error callback");
        NS_TEST_EXPECT_MSG_EQ(traced[DeferredDropReason::Overflow], 1, "Drop trace: overflow");
        NS_TEST_EXPECT_MSG_EQ(traced[DeferredDropReason::Expired], 1, "Drop trace: expired");
        NS_TEST_EXPECT_MSG_EQ(traced[DeferredDropReason::Discarded], 1, "Drop trace: discarded");
        q->Dispose();
    }
};

/// The queue's order, expiry and batched re-queue (#46).
class DeferredQueueOrderTestCase : public TestCase {
public:
    DeferredQueueOrderTestCase()
        : TestCase("glue: DeferredPacketQueue keeps order; Requeue is one batch") {}

private:
    static DeferredPacket Entry(Ipv4Address dst, uint32_t size) {
        DeferredPacket e;
        e.packet = Create<Packet>(size);
        e.header.SetDestination(dst);
        e.ecb = MakeCallback(&CountError);
        return e;
    }

    static std::vector<uint32_t> Sizes(const std::vector<DeferredPacket>& v) {
        std::vector<uint32_t> out;
        for (const auto& e : v) {
            out.push_back(e.packet->GetSize());
        }
        return out;
    }

    void DoRun() override {
        Ptr<DeferredPacketQueue> q = CreateObject<DeferredPacketQueue>();
        q->SetAttribute("MaxPackets", UintegerValue(6));
        q->SetAttribute("MaxAge", TimeValue(Seconds(1)));
        g_errors = 0;
        const Ipv4Address a("10.0.0.1");
        const Ipv4Address b("10.0.0.2");
        const Ipv4Address c("10.0.0.3");
        std::vector<uint32_t> firstA;
        std::vector<uint32_t> secondA;
        std::vector<uint32_t> restB;
        std::vector<Ipv4Address> dests;
        uint64_t enqueuedAfterRequeue = 0;
        uint32_t sizeAfterOverflow = 0;

        // t = 0.1 (not 0: an enqueue time of zero reads as "not yet queued"):
        // a1 b2 a3 c4 a5 b6, interleaved.
        const std::array<std::pair<Ipv4Address, uint32_t>, 6> fill{
            {{a, 1}, {b, 2}, {a, 3}, {c, 4}, {a, 5}, {b, 6}}};
        Simulator::Schedule(Seconds(0.1), [&]() {
            for (const auto& [d, n] : fill) {
                q->Enqueue(Entry(d, n));
            }
        });
        Simulator::Schedule(Seconds(0.5), [&]() {
            // Release a: oldest first; the others keep their order.
            std::vector<DeferredPacket> released = q->Release(a);
            firstA = Sizes(released);
            dests = q->Destinations();
            // No route yet: back as one batch, after b and c, in order, with
            // their first enqueue time (t = 0.1).
            q->Requeue(std::move(released));
            enqueuedAfterRequeue = q->Stats().enqueued;
            // A new one at t = 0.5: the queue is full, so b2 goes for room.
            q->Requeue(std::vector<DeferredPacket>{Entry(a, 7)});
        });
        Simulator::Schedule(Seconds(1.2), [&]() {
            // Everything enqueued at t = 0.1 has expired, the re-queued a1 a3 a5
            // too (their first enqueue time was kept); a7 (t = 0.5) has not.
            restB = Sizes(q->Release(b));
            secondA = Sizes(q->Release(a));
            // Overflow through Requeue drops the oldest, as Enqueue does.
            std::vector<DeferredPacket> many;
            for (uint32_t i = 10; i < 18; ++i) {
                many.push_back(Entry(c, i));
            }
            q->Requeue(std::move(many));
            sizeAfterOverflow = q->Size();
        });
        Simulator::Run();
        Simulator::Destroy();

        NS_TEST_EXPECT_MSG_EQ((firstA == std::vector<uint32_t>{1, 3, 5}), true,
                              "released oldest first");
        NS_TEST_EXPECT_MSG_EQ((dests == std::vector<Ipv4Address>{b, c}), true,
                              "the rest keep their order");
        NS_TEST_EXPECT_MSG_EQ(enqueuedAfterRequeue, 9u, "a requeued packet counts as enqueued");
        NS_TEST_EXPECT_MSG_EQ((secondA == std::vector<uint32_t>{7}), true,
                              "re-queued packets kept their first enqueue time");
        NS_TEST_EXPECT_MSG_EQ(restB.empty(), true, "expired packets are not released");
        NS_TEST_EXPECT_MSG_EQ(sizeAfterOverflow, 6u, "Requeue respects MaxPackets");
        const DeferredQueueStats& s = q->Stats();
        NS_TEST_EXPECT_MSG_EQ(s.expired, 5u, "c4 b6 and the re-queued a1 a3 a5 expired");
        NS_TEST_EXPECT_MSG_EQ(s.overflow, 3u, "b2, then two c packets, dropped for room");
        NS_TEST_EXPECT_MSG_EQ(s.enqueued,
                              s.released + s.overflow + s.expired + s.discarded + q->Size(),
                              "the counts add up");
        NS_TEST_EXPECT_MSG_EQ(g_errors, 8u, "every drop through the error callback");
        q->Dispose();
    }
};

/// The cached ports see a monitor aggregated after the first read (#46).
class PortCacheTestCase : public TestCase {
public:
    PortCacheTestCase() : TestCase("glue: cached ports follow a monitor added later") {}

private:
    void DoRun() override {
        NodeContainer nodes;
        nodes.Create(2);
        PointToPointHelper p2p;
        p2p.Install(nodes.Get(0), nodes.Get(1));
        InternetStackHelper stack;
        stack.SetRoutingHelper(SwarmRoutingHelper("ns3::swarmtest::ToyProtocol"));
        stack.Install(nodes);  // no monitors
        Ptr<ToyProtocol> toy = Toy(nodes.Get(0));
        const swarm::core::INeighborProvider* before = &toy->Neighbors();
        NS_TEST_EXPECT_MSG_EQ((dynamic_cast<const IslLinkMonitor*>(before) == nullptr), true,
                              "no monitor: the null port");
        NS_TEST_EXPECT_MSG_EQ((&toy->Neighbors() == before), true, "the port is cached");
        Ptr<IslLinkMonitor> isl = CreateObject<IslLinkMonitor>();
        isl->Install(nodes.Get(0));
        NS_TEST_EXPECT_MSG_EQ((&toy->Neighbors() == PeekPointer(isl)), true,
                              "a monitor aggregated later is seen");
        NS_TEST_EXPECT_MSG_EQ((&toy->LinkState() == PeekPointer(isl)), true,
                              "and is the link-state port");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(toy->LinkQuality().sinrDbFrom(1)), true,
                              "no wifi monitor: null link quality");
        Simulator::Destroy();
    }
};

/// A cancelled timer never runs, in the simulator (#46).
class CancellableTimerTestCase : public TestCase {
public:
    CancellableTimerTestCase() : TestCase("glue: SwarmTimerScheduler cancels a timer") {}

private:
    void DoRun() override {
        SwarmTimerScheduler timers;
        std::vector<std::string> ran;
        const swarm::core::TimerHandle stale =
            timers.scheduleCancellable(0.5, [&]() { ran.emplace_back("stale"); });
        const swarm::core::TimerHandle kept =
            timers.scheduleCancellable(0.6, [&]() { ran.emplace_back("kept"); });
        timers.scheduleCancellable(0.8, [&]() { ran.emplace_back("late"); });
        NS_TEST_EXPECT_MSG_NE(stale, swarm::core::kNoTimer, "a real handle");
        NS_TEST_EXPECT_MSG_NE(stale, kept, "distinct handles");
        NS_TEST_EXPECT_MSG_EQ(timers.Pending(), 3u, "three pending");
        std::size_t pendingAfterFire = 99;
        Simulator::Schedule(Seconds(0.1), [&]() { timers.cancel(stale); });
        Simulator::Schedule(Seconds(0.7), [&]() {
            pendingAfterFire = timers.Pending();
            timers.cancel(kept);  // already ran: a no-op
            timers.CancelAll();   // drops "late"
        });
        Simulator::Run();
        Simulator::Destroy();
        NS_TEST_EXPECT_MSG_EQ((ran == std::vector<std::string>{"kept"}), true,
                              "the cancelled timer never ran");
        NS_TEST_EXPECT_MSG_EQ(pendingAfterFire, 1u, "a timer that ran is forgotten");
        NS_TEST_EXPECT_MSG_EQ(timers.Pending(), 0u, "CancelAll forgets the rest");
    }
};

/// The Buffer::Iterator wire helpers write the core writer's bytes (#46).
class WireBufferTestCase : public TestCase {
public:
    WireBufferTestCase() : TestCase("glue: wire fields straight into an ns-3 Buffer") {}

private:
    template <typename W> static void Fields(W& w) {
        w.version(3);
        w.u16(0xBEEF);
        w.i32(-5);
        w.f64(1.25);
        w.boolean(true);
    }

    void DoRun() override {
        std::vector<uint8_t> ref;
        swarm::core::WireWriter rw(ref);
        Fields(rw);
        Buffer buf;
        buf.AddAtStart(static_cast<uint32_t>(ref.size()));
        Buffer::Iterator it = buf.Begin();
        BufferWireWriter bw(it);
        Fields(bw);
        NS_TEST_EXPECT_MSG_EQ(bw.size(), ref.size(), "every byte written");
        std::vector<uint8_t> got(buf.GetSize());
        buf.CopyData(got.data(), buf.GetSize());
        NS_TEST_EXPECT_MSG_EQ((got == ref), true, "same bytes as the vector writer");

        std::array<uint8_t, 32> scratch{};
        Buffer::Iterator rd = buf.Begin();
        swarm::core::WireReader r(ReadWireBytes(rd, scratch, static_cast<uint32_t>(ref.size())));
        NS_TEST_EXPECT_MSG_EQ(r.expectVersion(3), true, "version");
        NS_TEST_EXPECT_MSG_EQ(r.u16(), 0xBEEF, "u16");
        NS_TEST_EXPECT_MSG_EQ(r.i32(), -5, "i32");
        NS_TEST_EXPECT_MSG_EQ(r.f64(), 1.25, "f64");
        NS_TEST_EXPECT_MSG_EQ(r.boolean(), true, "bool");
        NS_TEST_EXPECT_MSG_EQ(r.atEnd(), true, "nothing left");

        Buffer::Iterator shortRd = buf.Begin();
        std::array<uint8_t, 4> tiny{};
        swarm::core::WireReader cut(
            ReadWireBytes(shortRd, tiny, static_cast<uint32_t>(ref.size())));
        cut.u8();
        cut.u16();
        cut.i32();
        NS_TEST_EXPECT_MSG_EQ(cut.ok(), false, "a short scratch fails the reader closed");
    }
};

// --- node lifecycle (#43)
// ----------------------------------------------------------

uint32_t LeavingFrom(Ptr<ToyProtocol> toy) {
    uint32_t n = 0;
    for (const auto& l : toy->controlLog) {
        n += l.size() > 2 && l.compare(l.size() - 2, 2, " 5") == 0 ? 1 : 0;
    }
    return n;
}

class LifecycleTestCase : public TestCase {
public:
    LifecycleTestCase()
        : TestCase("glue: shutdown and crash cancel timers, flush the queue, refuse "
                   "routing; only shutdown announces") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        Line line("10Mbps");
        Ptr<Node> n0 = line.nodes.Get(0);
        Ptr<Node> n1 = line.nodes.Get(1);
        Ptr<Node> n2 = line.nodes.Get(2);
        const Ipv4Address n0a = Addr(n0, 1);
        const Ipv4Address n2a = Addr(n2, 1);
        Toy(n1)->routes[n0a] = n0a;

        std::vector<std::string> lifecycle;
        Toy(n0)->TraceConnectWithoutContext(
            "Lifecycle",
            MakeBoundCallback(
                +[](std::vector<std::string>* out, SwarmNodeState st, SwarmLifecycleOp op) {
                    out->push_back(std::to_string(static_cast<int>(st)) + ":" +
                                   std::to_string(static_cast<int>(op)));
                },
                &lifecycle));
        std::map<DeferredDropReason, int> drops;
        Toy(n0)->GetDeferredQueue()->TraceConnectWithoutContext(
            "Drop", MakeBoundCallback(
                        +[](std::map<DeferredDropReason, int>* m, Ptr<const Packet>,
                            const Ipv4Header&, DeferredDropReason r) { ++(*m)[r]; },
                        &drops));

        std::vector<std::string> rx0;
        Ptr<Socket> sink0 = Sink(n0, &rx0);
        Ptr<Socket> src0 = Source(n0);
        Ptr<Socket> src1 = Source(n1);
        bool downDuring = false;
        int sendWhileDown = 0;
        Simulator::Schedule(Seconds(0.5), [&]() {
            SendUdp(src0, n2a, 100);  // no route at n0: deferred
            Toy(n0)->ArmProbe(2.5);   // would run at 3.0
        });
        Simulator::Schedule(Seconds(1.0), [&]() { Toy(n0)->Shutdown(); });
        Simulator::Schedule(Seconds(1.5), [&]() {
            downDuring = Toy(n0)->IsDown();
            sendWhileDown = src0->SendTo(Create<Packet>(100), 0, InetSocketAddress(n2a, kDataPort));
            SendUdp(src1, n0a, 100);  // reaches n0, which routes nothing while down
            Toy(n0)->Shutdown();      // already down: a no-op
        });
        Simulator::Schedule(Seconds(2.0), [&]() { Toy(n2)->Crash(); });
        Simulator::Schedule(Seconds(4.0), [&]() { Toy(n0)->Start(); });
        Simulator::Schedule(Seconds(4.5), [&]() { SendUdp(src1, n0a, 100); });
        Simulator::Stop(Seconds(5));
        Simulator::Run();

        const DeferredQueueStats& s = Toy(n0)->GetDeferredQueue()->Stats();
        NS_TEST_EXPECT_MSG_EQ(s.nodeDown, 1u, "the held packet is dropped as NodeDown");
        NS_TEST_EXPECT_MSG_EQ(drops[DeferredDropReason::NodeDown], 1, "through the Drop trace");
        NS_TEST_EXPECT_MSG_EQ(s.enqueued, 1u, "nothing is deferred while down");
        NS_TEST_EXPECT_MSG_EQ(s.enqueued,
                              s.released + s.overflow + s.expired + s.discarded + s.nodeDown +
                                  Toy(n0)->GetDeferredQueue()->Size(),
                              "the counts add up");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->probeRan, false, "the core's timer was cancelled");
        NS_TEST_EXPECT_MSG_EQ(downDuring, true, "down between Shutdown and Start");
        NS_TEST_EXPECT_MSG_EQ(sendWhileDown, -1, "RouteOutput refuses while down");
        NS_TEST_EXPECT_MSG_EQ(rx0.size(), 1u, "RouteInput refuses while down, delivers after");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->IsDown(), false, "up after Start");
        NS_TEST_EXPECT_MSG_EQ(
            (lifecycle == std::vector<std::string>{"1:1", "0:0"}), true,
            "Lifecycle trace: (Down, Shutdown), (Up, Start); the repeat is silent");
        NS_TEST_EXPECT_MSG_EQ(
            (Toy(n0)->lifecycleLog == std::vector<std::string>{"shutdown", "start"}), true,
            "n0's hooks");
        NS_TEST_EXPECT_MSG_EQ((Toy(n2)->lifecycleLog == std::vector<std::string>{"crash"}), true,
                              "n2's hook");
        // Shutdown and crash differ observably: n1 heard n0 leave, not n2.
        NS_TEST_EXPECT_MSG_EQ(LeavingFrom(Toy(n1)), 1u, "one leaving control, from n0");
        bool fromN0 = false;
        for (const auto& l : Toy(n1)->controlLog) {
            std::ostringstream want;
            want << n0a << "->";
            fromN0 |= l.find(want.str()) != std::string::npos;
        }
        NS_TEST_EXPECT_MSG_EQ(fromN0, true, "the leaving control came from n0");
        Simulator::Destroy();
    }
};

/// n0 - n1 - n2 - n3 - n4 over point-to-point links, n0 sending to n4 every
/// 250 ms; the helper crashes n2 at 2 s and restarts it at 4 s from a
/// schedule file. Returns the deliveries at n4 (time in ns, source).
std::vector<std::string> RunChainWithFailures(const std::string& schedule, uint32_t seed) {
    SwarmCoreSeed(seed);
    NodeContainer nodes;
    nodes.Create(5);
    PointToPointHelper p2p;
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    std::vector<NetDeviceContainer> links;
    for (uint32_t i = 0; i + 1 < 5; ++i) {
        links.push_back(p2p.Install(nodes.Get(i), nodes.Get(i + 1)));
    }
    SwarmRoutingHelper toy("ns3::swarmtest::ToyProtocol");
    toy.Install(nodes);
    toy.AssignStreams(nodes, 0);
    Ipv4AddressHelper a;
    for (uint32_t i = 0; i < links.size(); ++i) {
        std::ostringstream base;
        base << "10.2." << i << ".0";
        a.SetBase(base.str().c_str(), "255.255.255.0");
        a.Assign(links[i]);
    }
    const Ipv4Address dst("10.2.3.2");  // n4 on its only link
    for (uint32_t i = 0; i < 4; ++i) {
        std::ostringstream next;
        next << "10.2." << i << ".2";
        Toy(nodes.Get(i))->staticRoutes[dst] = Ipv4Address(next.str().c_str());
        Toy(nodes.Get(i))->routes = Toy(nodes.Get(i))->staticRoutes;
    }
    std::ofstream(schedule) << "# time node op\n2.0 2 crash\n\n4.0 2 start  # back\n";
    SwarmLifecycleHelper().ScheduleFile(schedule, nodes);

    auto* rx = new std::vector<std::string>();
    Ptr<Socket> sink = Sink(nodes.Get(4), rx);
    Ptr<Socket> src = Source(nodes.Get(0));
    for (int k = 2; k < 24; ++k) {
        Simulator::Schedule(MilliSeconds(250 * k), [src, dst]() { SendUdp(src, dst, 100); });
    }
    Simulator::Stop(Seconds(6.5));
    Simulator::Run();
    Simulator::Destroy();
    std::vector<std::string> out = *rx;
    delete rx;
    return out;
}

class LifecycleHelperTestCase : public TestCase {
public:
    LifecycleHelperTestCase()
        : TestCase("glue: SwarmLifecycleHelper crashes and restarts a chain node; delivery "
                   "recovers; same seed, same bytes") {}

private:
    void DoRun() override {
        const std::string schedule = CreateTempDirFilename("failures.txt");
        const std::vector<std::string> first = RunChainWithFailures(schedule, 3);
        uint32_t before = 0;
        uint32_t during = 0;
        uint32_t after = 0;
        for (const auto& l : first) {
            const double t = std::stod(l.substr(0, l.find(' '))) * 1e-9;
            (t < 2.0 ? before : t < 4.0 ? during : after) += 1;
        }
        NS_TEST_EXPECT_MSG_EQ(before, 6u, "delivered before the crash (0.5 .. 1.75 s)");
        NS_TEST_EXPECT_MSG_EQ(during, 0u, "nothing through the crashed relay");
        NS_TEST_EXPECT_MSG_EQ(after, 8u, "delivery recovers after the restart (4.0 .. 5.75 s)");
        // Determinism (org rule 5): the same seed and schedule, the same bytes.
        NS_TEST_EXPECT_MSG_EQ((RunChainWithFailures(schedule, 3) == first), true,
                              "byte-identical rerun");

        SwarmLifecycleOp op = SwarmLifecycleOp::Start;
        NS_TEST_EXPECT_MSG_EQ(SwarmLifecycleHelper::ParseOp("shutdown", op) &&
                                  op == SwarmLifecycleOp::Shutdown,
                              true, "parse shutdown");
        NS_TEST_EXPECT_MSG_EQ(SwarmLifecycleHelper::ParseOp("reboot", op), false,
                              "an unknown operation is refused");
    }
};

// --- SwarmRoutingBase
// -------------------------------------------------------------

class RoutingBaseTestCase : public TestCase {
public:
    RoutingBaseTestCase()
        : TestCase("glue: SwarmRoutingBase defers, releases, forwards, carries "
                   "control") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        Line line("10Mbps");
        Ptr<Node> n0 = line.nodes.Get(0);
        Ptr<Node> n1 = line.nodes.Get(1);
        Ptr<Node> n2 = line.nodes.Get(2);
        const Ipv4Address n0a = Addr(n0, 1);
        const Ipv4Address n1a = Addr(n1, 1);  // toward n0
        const Ipv4Address n1b = Addr(n1, 2);  // toward n2
        const Ipv4Address n2a = Addr(n2, 1);
        Toy(n0)->pending[n2a] = n1a;  // learned 0.5 s after discovery starts
        Toy(n1)->routes[n2a] = n2a;
        const Ipv4Address nowhere("10.9.9.9");

        std::vector<std::string> rx;
        Ptr<Socket> sink = Sink(n2, &rx);
        Ptr<Socket> src = Source(n0);
        Simulator::Schedule(Seconds(0.2), [&]() {
            for (uint32_t i = 0; i < 3; ++i) {
                Toy(line.nodes.Get(i))->SendHelloNow();
            }
        });
        Simulator::Schedule(Seconds(0.4), [&]() { Toy(n0)->UnicastMessage(n1a, 7); });
        Simulator::Schedule(Seconds(1.0), [&]() { SendUdp(src, n2a, 100); });
        Simulator::Schedule(Seconds(1.1), [&]() { SendUdp(src, nowhere, 100); });
        Simulator::Schedule(Seconds(1.2), [&]() { Toy(n0)->DiscardViaCore(nowhere); });
        Simulator::Stop(Seconds(3));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ(rx.size(), 1u, "the deferred packet arrives once its route exists");
        Ptr<DeferredPacketQueue> q = Toy(n0)->GetDeferredQueue();
        NS_TEST_EXPECT_MSG_EQ(q->Stats().enqueued, 2u, "both unroutable packets were deferred");
        NS_TEST_EXPECT_MSG_EQ(q->Stats().released, 1u, "one released");
        NS_TEST_EXPECT_MSG_EQ(q->Stats().discarded, 1u, "one discarded by the core's decision");
        NS_TEST_EXPECT_MSG_EQ_TOL(q->Stats().releasedHoldSumS, 0.5, 1e-6,
                                  "held for the discovery delay");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->discoveries, 2u, "one discovery per deferral");
        NS_TEST_ASSERT_MSG_EQ(Toy(n1)->dataFrom.size(), 1u, "n1 saw the data packet");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->dataFrom[0], n0a, "previous hop at n1");
        NS_TEST_ASSERT_MSG_EQ(Toy(n2)->dataFrom.size(), 1u, "n2 saw the data packet");
        NS_TEST_EXPECT_MSG_EQ(Toy(n2)->dataFrom[0], n1b, "previous hop at n2");
        // Hellos: n1 hears n0 and n2; n0 and n2 hear n1. Plus n0's unicast to n1.
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->controlLog.size(), 1u, "n0 control");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->controlLog.size(), 3u, "n1 control");
        NS_TEST_EXPECT_MSG_EQ(Toy(n2)->controlLog.size(), 1u, "n2 control");
        bool unicastSeen = false;
        for (const auto& l : Toy(n1)->controlLog) {
            unicastSeen |= l.size() > 2 && l.compare(l.size() - 2, 2, " 7") == 0;
        }
        NS_TEST_EXPECT_MSG_EQ(unicastSeen, true, "Execute(unicast) reached n1 with its size");
        Simulator::Destroy();
    }
};

/// An adapter that overrides RouteInput holds a transit packet through the
/// protected Defer, as given (Termite's header stays on), and the base
/// releases it unchanged once the route is learned (#40).
class DeferFromAdapterTestCase : public TestCase {
public:
    DeferFromAdapterTestCase()
        : TestCase("glue: an adapter's RouteInput holds a packet through Defer, "
                   "as given") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        Line line("10Mbps");
        Ptr<Node> n0 = line.nodes.Get(0);
        Ptr<Node> n1 = line.nodes.Get(1);
        Ptr<Node> n2 = line.nodes.Get(2);
        const Ipv4Address n1a = Addr(n1, 1);
        const Ipv4Address n2a = Addr(n2, 1);
        Toy(n0)->routes[n2a] = n1a;
        Toy(n1)->pending[n2a] = n2a;  // learned 0.5 s after the relay's discovery
        Toy(n1)->holdMarked = true;

        std::vector<std::string> rx;
        Ptr<Socket> sink = Sink(n2, &rx);
        Ptr<Socket> src = Source(n0);
        Simulator::Schedule(Seconds(1.0), [&]() { SendUdp(src, n2a, 100); });
        Simulator::Stop(Seconds(3));
        Simulator::Run();

        Ptr<DeferredPacketQueue> q = Toy(n1)->GetDeferredQueue();
        NS_TEST_EXPECT_MSG_EQ(q->Stats().enqueued, 1u, "the relay held the packet");
        NS_TEST_EXPECT_MSG_EQ(q->Stats().released, 1u, "and released it");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->discoveries, 1u, "Defer started the relay's discovery");
        NS_TEST_EXPECT_MSG_EQ(rx.size(), 1u, "the held packet reached the destination");
        NS_TEST_ASSERT_MSG_EQ(Toy(n2)->markedFrom.size(), 1u, "n2 saw the data packet");
        NS_TEST_EXPECT_MSG_EQ(Toy(n2)->markedFrom[0], true,
                              "what the adapter put on the packet was released with it");
        Simulator::Destroy();
    }
};

// --- the router observer
// -------------------------------------------------------------

/// Appends one line per trace-source event to `log`.
void ConnectObserverTraces(Ptr<SwarmRoutingBase> p, std::vector<std::string>* log) {
    p->TraceConnectWithoutContext(
        "ControlTx",
        MakeBoundCallback(
            +[](std::vector<std::string>* out, uint8_t kind, bool broadcast, uint32_t bytes) {
                out->push_back("tx " + std::to_string(kind) + (broadcast ? " b " : " u ") +
                               std::to_string(bytes));
            },
            log));
    p->TraceConnectWithoutContext(
        "ControlRx",
        MakeBoundCallback(
            +[](std::vector<std::string>* out, uint8_t kind, uint32_t bytes) {
                out->push_back("rx " + std::to_string(kind) + " " + std::to_string(bytes));
            },
            log));
    p->TraceConnectWithoutContext(
        "RouteChanged",
        MakeBoundCallback(
            +[](std::vector<std::string>* out, Ipv4Address dest, Ipv4Address next, bool added) {
                std::ostringstream os;
                os << "route " << dest << " " << next << " " << added;
                out->push_back(os.str());
            },
            log));
    p->TraceConnectWithoutContext(
        "DataDecision",
        MakeBoundCallback(
            +[](std::vector<std::string>* out, Ipv4Address dest, swarm::core::RouteAction a) {
                std::ostringstream os;
                os << "data " << dest << " " << static_cast<int>(a);
                out->push_back(os.str());
            },
            log));
    p->TraceConnectWithoutContext(
        "StateSize",
        MakeBoundCallback(
            +[](std::vector<std::string>* out, uint32_t entries, uint32_t bytes) {
                out->push_back("state " + std::to_string(entries) + " " + std::to_string(bytes));
            },
            log));
}

class ObserverTestCase : public TestCase {
public:
    ObserverTestCase()
        : TestCase("glue: the router observer's trace sources, counters, kind names") {}

private:
    void DoRun() override {
        using swarm::core::RouteAction;
        SwarmCoreSeed(1);
        Line line("10Mbps");
        Ptr<Node> n0 = line.nodes.Get(0);
        Ptr<Node> n1 = line.nodes.Get(1);
        Ptr<Node> n2 = line.nodes.Get(2);
        const Ipv4Address n1a = Addr(n1, 1);
        const Ipv4Address n2a = Addr(n2, 1);
        Toy(n0)->pending[n2a] = n1a;
        Toy(n1)->routes[n2a] = n2a;
        std::vector<std::string> log0;
        std::vector<std::string> log1;
        std::vector<std::string> log2;
        ConnectObserverTraces(Toy(n0), &log0);
        ConnectObserverTraces(Toy(n1), &log1);
        ConnectObserverTraces(Toy(n2), &log2);

        std::vector<std::string> rx;
        Ptr<Socket> sink = Sink(n2, &rx);
        Ptr<Socket> src = Source(n0);
        Simulator::Schedule(Seconds(0.2), [&]() {
            for (uint32_t i = 0; i < 3; ++i) {
                Toy(line.nodes.Get(i))->SendHelloNow();
            }
        });
        Simulator::Schedule(Seconds(0.4), [&]() { Toy(n0)->UnicastMessage(n1a, 7); });
        Simulator::Schedule(Seconds(1.0), [&]() { SendUdp(src, n2a, 100); });
        Simulator::Stop(Seconds(3));
        Simulator::Run();
        NS_TEST_ASSERT_MSG_EQ(rx.size(), 1u, "the packet arrives");

        // Every event reached its trace source, in order.
        std::ostringstream route;
        route << "route " << n2a << " " << n1a << " 1";
        std::ostringstream toN2;
        toN2 << n2a;
        const std::vector<std::string> expected0{
            "tx 1 b 4",
            "rx 1 4",
            "tx 2 u 7",
            "data " + toN2.str() + " " + std::to_string(static_cast<int>(RouteAction::Queue)),
            route.str(),
            "state 1 8",
            "data " + toN2.str() + " " + std::to_string(static_cast<int>(RouteAction::Unicast)),
        };
        NS_TEST_ASSERT_MSG_EQ(log0.size(), expected0.size(), "n0 events");
        for (size_t i = 0; i < expected0.size(); ++i) {
            NS_TEST_EXPECT_MSG_EQ(log0[i], expected0[i], "n0 event " << i);
        }
        NS_TEST_EXPECT_MSG_EQ(log1.size(), 5u, "n1: hello out, 2 hellos + 1 message in, forward");
        NS_TEST_EXPECT_MSG_EQ(log1.back(),
                              "data " + toN2.str() + " " +
                                  std::to_string(static_cast<int>(RouteAction::Unicast)),
                              "n1 forwards");
        NS_TEST_EXPECT_MSG_EQ(log2.size(), 3u, "n2: hello out, hello in, delivery");
        NS_TEST_EXPECT_MSG_EQ(log2.back(),
                              "data " + toN2.str() + " " +
                                  std::to_string(static_cast<int>(RouteAction::Deliver)),
                              "n2 delivers");

        // The per-node counters and their aggregate.
        const SwarmRoutingStats stats = SwarmRoutingHelper::CollectStats(line.nodes);
        NS_TEST_ASSERT_MSG_EQ(stats.perNode.size(), 3u, "one entry per node");
        NS_TEST_EXPECT_MSG_EQ(stats.perNode[0].first, n0->GetId(), "in container order");
        const swarm::core::RouterStats& s0 = stats.perNode[0].second;
        NS_TEST_EXPECT_MSG_EQ(s0.control.at(kHello).txBroadcasts, 1u, "n0 hello");
        NS_TEST_EXPECT_MSG_EQ(s0.control.at(kMessage).txBytes, 7u, "n0 message bytes");
        NS_TEST_EXPECT_MSG_EQ(s0.routeAdds, 1u, "n0 learned one route");
        NS_TEST_EXPECT_MSG_EQ(s0.peakStateEntries, 1u, "n0 peak state");
        NS_TEST_EXPECT_MSG_EQ(s0.decisions(RouteAction::Queue), 1u, "n0 queued");
        NS_TEST_EXPECT_MSG_EQ_TOL(s0.observedFor, 3.0, 1e-9, "window: the run");
        // One entry from t = 1.5 (1.0 + the 0.5 s discovery) to 3: 1.5 / 3.
        NS_TEST_EXPECT_MSG_EQ_TOL(s0.meanStateEntries, 0.5, 1e-6, "n0 time-averaged state");
        const swarm::core::RouterStats& all = stats.aggregate;
        NS_TEST_EXPECT_MSG_EQ(all.control.size(), 2u, "two kinds");
        NS_TEST_EXPECT_MSG_EQ(all.control.at(kHello).txPackets, 3u, "three hellos sent");
        NS_TEST_EXPECT_MSG_EQ(all.control.at(kHello).rxPackets, 4u, "four hellos heard");
        NS_TEST_EXPECT_MSG_EQ(all.control.at(kHello).rxBytes, 16u, "hello bytes heard");
        NS_TEST_EXPECT_MSG_EQ(all.control.at(kMessage).rxPackets, 1u, "the message heard");
        NS_TEST_EXPECT_MSG_EQ(all.decisions(RouteAction::Unicast), 2u, "two forwards");
        NS_TEST_EXPECT_MSG_EQ(all.decisions(RouteAction::Deliver), 1u, "one delivery");
        NS_TEST_EXPECT_MSG_EQ(all.decisions(RouteAction::Drop), 0u, "no drop");

        // Kind names, registered once per protocol.
        SwarmRoutingHelper toy("ns3::swarmtest::ToyProtocol");
        toy.SetKindNames(kToyKinds);
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->KindName(kHello), std::string("HELLO"), "named kind");
        NS_TEST_EXPECT_MSG_EQ(toy.KindName(kMessage), std::string("MSG"),
                              "named through the helper");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->KindName(9), std::string("9"),
                              "an unnamed kind prints its number");
        NS_TEST_EXPECT_MSG_EQ(SwarmRoutingBase::GetKindName(SwarmRoutingBase::GetTypeId(), kHello),
                              std::string("1"), "names belong to their protocol");
        // A parent's names reach its children; a null name is skipped.
        const std::array<swarm::core::KindName, 2> baseKinds{{{7, "BASE"}, {8, nullptr}}};
        SwarmRoutingBase::SetKindNames(SwarmRoutingBase::GetTypeId(), baseKinds);
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->KindName(7), std::string("BASE"), "inherited name");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->KindName(8), std::string("8"), "null name skipped");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->KindName(kHello), std::string("HELLO"), "own name first");
        SwarmRoutingBase::SetKindNames(SwarmRoutingBase::GetTypeId(), {});
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->KindName(7), std::string("7"), "names replaced");

        Toy(n0)->ResetRouterStats();
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->GetRouterStats().control.empty(), true, "reset");
        Simulator::Destroy();
    }
};

// --- IslLinkMonitor
// ----------------------------------------------------------------

class IslMonitorTestCase : public TestCase {
public:
    IslMonitorTestCase()
        : TestCase("glue: IslLinkMonitor neighbours, queue, service time, link "
                   "down and up") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        Line line("1Mbps");
        Ptr<Node> n0 = line.nodes.Get(0);
        Ptr<Node> n1 = line.nodes.Get(1);
        Ptr<Node> n2 = line.nodes.Get(2);
        const Ipv4Address n0a = Addr(n0, 1);
        const Ipv4Address n1a = Addr(n1, 1);
        const Ipv4Address n2a = Addr(n2, 1);
        Toy(n0)->routes[n1a] = n1a;
        Ptr<IslLinkMonitor> m0 = n0->GetObject<IslLinkMonitor>();
        Ptr<IslLinkMonitor> m1 = n1->GetObject<IslLinkMonitor>();
        NS_TEST_ASSERT_MSG_EQ(static_cast<bool>(m0), true, "the helper installed the monitor");
        NS_TEST_ASSERT_MSG_EQ(static_cast<bool>(m1), true, "on every node with a p2p device");
        NS_TEST_ASSERT_MSG_EQ(static_cast<bool>(n0->GetObject<WifiLinkMonitor>()), false,
                              "no wifi, no monitor");

        std::vector<swarm::core::NodeAddress> before;
        std::vector<swarm::core::NodeAddress> during;
        std::vector<swarm::core::NodeAddress> portView;
        int backlog = -1;
        int backlogAll = -1;
        double service = -1.0;
        std::vector<std::string> rx;
        Ptr<Socket> sink = Sink(n1, &rx);
        Ptr<Socket> src = Source(n0);
        const uint32_t ifToN2 = n1->GetObject<Ipv4>()->GetInterfaceForDevice(line.d12.Get(0));

        Simulator::Schedule(Seconds(0.1), [&]() {
            before = m1->neighbors();
            portView = Toy(n1)->Neighbors().neighbors();
        });
        Simulator::Schedule(Seconds(1.0), [&]() {
            for (int i = 0; i < 20; ++i) {
                SendUdp(src, n1a, 1000);
            }
        });
        Simulator::Schedule(Seconds(1.0) + MicroSeconds(1), [&]() {
            backlog = m0->macQueueLength(ToNodeAddress(n1a));
            backlogAll = Toy(n0)->LinkState().macQueueLength(swarm::core::kInvalidAddress);
        });
        Simulator::Schedule(Seconds(2.0),
                            [&]() { service = m0->macServiceTime(ToNodeAddress(n1a)); });
        Simulator::Schedule(Seconds(3.0), [&]() { n1->GetObject<Ipv4>()->SetDown(ifToN2); });
        Simulator::Schedule(Seconds(3.1), [&]() { during = m1->neighbors(); });
        Simulator::Schedule(Seconds(4.0), [&]() { n1->GetObject<Ipv4>()->SetUp(ifToN2); });
        Simulator::Stop(Seconds(5));
        Simulator::Run();

        NS_TEST_ASSERT_MSG_EQ(before.size(), 2u, "n1 has two ISL neighbours");
        NS_TEST_EXPECT_MSG_EQ(ToIpv4Address(before[0]), n0a, "n0, in device order");
        NS_TEST_EXPECT_MSG_EQ(ToIpv4Address(before[1]), n2a, "n2");
        NS_TEST_EXPECT_MSG_EQ((portView == before), true, "the base's Neighbors() port");
        NS_TEST_EXPECT_MSG_GT(backlog, 10, "a burst backs up the 1 Mb/s link");
        NS_TEST_EXPECT_MSG_EQ(backlogAll, backlog, "one link: aggregate == per next hop");
        // 1000 B payload + UDP 8 + IP 20 + PPP 2 at 1 Mb/s.
        NS_TEST_EXPECT_MSG_EQ_TOL(service, 1030 * 8 / 1e6, 1e-6,
                                  "service time is serialisation only");
        NS_TEST_EXPECT_MSG_EQ(rx.size(), 20u, "the burst is delivered");
        NS_TEST_ASSERT_MSG_EQ(during.size(), 1u, "the cut ISL is gone");
        NS_TEST_EXPECT_MSG_EQ(ToIpv4Address(during[0]), n0a, "n0 remains");
        NS_TEST_ASSERT_MSG_EQ(Toy(n1)->failures.size(), 1u, "OnLinkFailure on interface down");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->failures[0], n2a, "the peer of the cut link");
        bool upSeen = false;
        for (const auto& u : Toy(n1)->ups) {
            upSeen |= u == n2a;
        }
        NS_TEST_EXPECT_MSG_EQ(upSeen, true, "OnLinkUp when it comes back");
        Simulator::Destroy();
    }
};

// --- wifi
// -----------------------------------------------------------------------------

/// Ad hoc 802.11a at a fixed 6 Mb/s, nodes on a line `spacing` metres apart.
NetDeviceContainer AdhocWifi(NodeContainer& nodes, double spacing) {
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        pos->Add(Vector(spacing * i, 0.0, 0.0));
    }
    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                 StringValue("OfdmRate6Mbps"), "ControlMode",
                                 StringValue("OfdmRate6Mbps"));
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    return wifi.Install(phy, mac, nodes);
}

// --- neighbour discovery from link state on point-to-point links (#4)
// ------------------------------------------------

/// What a three-node ISL line did: every hello and data packet received, with
/// its time, and what n1's protocol learned. One string, so two runs compare
/// byte for byte.
struct IslHelloRun {
    std::string log;
    uint32_t hellosBeforeCut = 0;    ///< hellos received anywhere, t < 5 s
    uint32_t hellosN1toN2After = 0;  ///< hellos n2 had from n1, t > 6 s
    uint32_t delivered = 0;
    std::vector<Ipv4Address> n1Ups;
    std::vector<Ipv4Address> n1Failures;
    bool n2LinkStateAt1 = false;
    bool n2LinkStateAfter = false;
    bool n0LinkStateAfter = false;
    bool n1SuppressedToN0After = false;
    bool n1SuppressedToN2After = false;
    std::vector<Ipv4Address> n2Failures;
    bool n1LinkStateAtN2After = true;  ///< n2's view of n1 after the one-sided cut
    bool n2SuppressedToN1After = true;
};

/// n0 - n1 - n2 over 10 Mb/s point-to-point links, a hello from every node
/// every 0.25 s on average, 20 data packets n0 -> n2 at 2 s and 10 more at
/// 7 s; n1's interface toward n2 goes down at 5 s and comes back at 6 s.
/// `beacon`: hellos through BroadcastBeacon; `suppress`: IslHelloSuppression.
IslHelloRun RunIslHelloScenario(uint32_t seed, bool beacon, bool suppress) {
    SwarmCoreSeed(seed);
    Line line("10Mbps", suppress);
    Ptr<Node> n0 = line.nodes.Get(0);
    Ptr<Node> n1 = line.nodes.Get(1);
    Ptr<Node> n2 = line.nodes.Get(2);
    const Ipv4Address n0a = Addr(n0, 1);
    const Ipv4Address n1a = Addr(n1, 1);
    const Ipv4Address n2a = Addr(n2, 1);
    for (uint32_t i = 0; i < 3; ++i) {
        Toy(line.nodes.Get(i))->helloInterval = 0.25;
        Toy(line.nodes.Get(i))->beacon = beacon;
    }
    Toy(n0)->routes[n2a] = n1a;
    Toy(n1)->routes[n2a] = n2a;
    std::vector<std::string> rx;
    Ptr<Socket> sink = Sink(n2, &rx);
    Ptr<Socket> src = Source(n0);
    const uint32_t ifToN0 = n1->GetObject<Ipv4>()->GetInterfaceForDevice(line.d01.Get(1));
    const uint32_t ifToN2 = n1->GetObject<Ipv4>()->GetInterfaceForDevice(line.d12.Get(0));
    IslHelloRun run;

    Simulator::Schedule(Seconds(1.0),
                        [&]() { run.n2LinkStateAt1 = Toy(n1)->IsLinkStateNeighbor(n2a); });
    for (const double t : {2.0, 7.0}) {
        Simulator::Schedule(Seconds(t), [&, t]() {
            for (int i = 0; i < (t < 5.0 ? 20 : 10); ++i) {
                SendUdp(src, n2a, 200);
            }
        });
    }
    Simulator::Schedule(Seconds(5.0), [&]() { n1->GetObject<Ipv4>()->SetDown(ifToN2); });
    Simulator::Schedule(Seconds(6.0), [&]() { n1->GetObject<Ipv4>()->SetUp(ifToN2); });
    Simulator::Schedule(Seconds(6.5), [&]() {
        run.n2LinkStateAfter = Toy(n1)->IsLinkStateNeighbor(n2a);
        run.n0LinkStateAfter = Toy(n1)->IsLinkStateNeighbor(n0a);
        run.n1SuppressedToN0After = Toy(n1)->BeaconSuppressed(ifToN0);
        run.n1SuppressedToN2After = Toy(n1)->BeaconSuppressed(ifToN2);
        run.n1LinkStateAtN2After = Toy(n2)->IsLinkStateNeighbor(Addr(n1, 2));
        run.n2SuppressedToN1After = Toy(n2)->BeaconSuppressed(1);
    });
    Simulator::Stop(Seconds(10));
    Simulator::Run();

    std::ostringstream out;
    for (uint32_t i = 0; i < 3; ++i) {
        for (const std::string& l : Toy(line.nodes.Get(i))->controlLog) {
            out << "ctl n" << i << " " << l << "\n";
            std::istringstream fields(l);
            double t = 0.0;
            std::string link;
            fields >> t >> link;
            run.hellosBeforeCut += t < 5.0 ? 1 : 0;
            if (i == 2 && t > 6.0) {
                std::ostringstream from;
                from << Addr(n1, 2) << "->";
                run.hellosN1toN2After += link.rfind(from.str(), 0) == 0 ? 1 : 0;
            }
        }
    }
    for (const std::string& l : rx) {
        out << "rx " << l << "\n";
    }
    run.delivered = static_cast<uint32_t>(rx.size());
    run.n1Ups = Toy(n1)->ups;
    run.n1Failures = Toy(n1)->failures;
    run.n2Failures = Toy(n2)->failures;
    for (const Ipv4Address& u : run.n1Ups) {
        out << "up " << u << "\n";
    }
    for (const Ipv4Address& f : run.n1Failures) {
        out << "fail " << f << "\n";
    }
    for (const Ipv4Address& f : run.n2Failures) {
        out << "n2 fail " << f << "\n";
    }
    run.log = out.str();
    Simulator::Destroy();
    return run;
}

bool Contains(const std::vector<Ipv4Address>& v, Ipv4Address a) {
    for (const Ipv4Address& x : v) {
        if (x == a) {
            return true;
        }
    }
    return false;
}

class IslHelloSuppressionTestCase : public TestCase {
public:
    IslHelloSuppressionTestCase()
        : TestCase("glue: ISL neighbours from link state, beacons suppressed, the seam "
                   "gate, delivery unchanged, off path byte-identical (#4)") {}

private:
    void DoRun() override {
        const IslHelloRun control = RunIslHelloScenario(1, false, false);
        const IslHelloRun beaconOff = RunIslHelloScenario(1, true, false);
        const IslHelloRun on = RunIslHelloScenario(1, true, true);
        const IslHelloRun onAgain = RunIslHelloScenario(1, true, true);

        // Off, a beacon is a broadcast: byte-identical output.
        NS_TEST_EXPECT_MSG_EQ(beaconOff.log, control.log,
                              "IslHelloSuppression off: BroadcastBeacon == BroadcastControl");
        NS_TEST_EXPECT_MSG_EQ(on.log, onAgain.log, "on, same seed: identical output");
        NS_TEST_EXPECT_MSG_EQ(control.n2LinkStateAt1, false, "off: no link-state neighbours");

        // On: no hello on an authoritative ISL; the control count drops.
        NS_TEST_EXPECT_MSG_GT(control.hellosBeforeCut, 40u, "off: hellos on every ISL");
        NS_TEST_EXPECT_MSG_EQ(on.hellosBeforeCut, 0u, "on: no hello before any link fails");
        // Neighbours learned without hello, at start.
        const Ipv4Address n0a("10.1.1.1");
        const Ipv4Address n2a("10.1.2.2");
        NS_TEST_EXPECT_MSG_EQ(Contains(on.n1Ups, n0a), true, "n1 learns n0 from link state");
        NS_TEST_EXPECT_MSG_EQ(Contains(on.n1Ups, n2a), true, "n1 learns n2 from link state");
        NS_TEST_EXPECT_MSG_EQ(on.n2LinkStateAt1, true, "n2 is a link-state neighbour of n1");
        // Neighbour loss from link state, with hello disabled on that ISL.
        NS_TEST_ASSERT_MSG_EQ(on.n1Failures.size(), 1u, "one loss, from the interface down");
        NS_TEST_EXPECT_MSG_EQ(on.n1Failures[0], n2a, "the peer of the cut ISL");
        // The seam gate: a link that failed once is beaconed again.
        NS_TEST_EXPECT_MSG_EQ(on.n2LinkStateAfter, false, "after a failure: hello's again");
        NS_TEST_EXPECT_MSG_EQ(on.n1SuppressedToN2After, false, "the failed ISL beacons");
        NS_TEST_EXPECT_MSG_EQ(on.n0LinkStateAfter, true, "the untouched ISL stays link-state");
        NS_TEST_EXPECT_MSG_EQ(on.n1SuppressedToN0After, true, "and stays silent");
        NS_TEST_EXPECT_MSG_GT(on.hellosN1toN2After, 0u, "n2 hears n1's hellos again");
        // The far end of a one-sided cut (ns-3 downs n1's interface only)
        // loses the carrier too: n2 learns the loss and beacons again.
        const Ipv4Address n1toN2("10.1.2.1");
        NS_TEST_ASSERT_MSG_EQ(on.n2Failures.size(), 1u, "n2 hears of the cut from the carrier");
        NS_TEST_EXPECT_MSG_EQ(on.n2Failures[0], n1toN2, "n1, on that link");
        NS_TEST_EXPECT_MSG_EQ(on.n1LinkStateAtN2After, false, "n2 no longer trusts link state");
        NS_TEST_EXPECT_MSG_EQ(on.n2SuppressedToN1After, false, "and beacons toward n1");
        NS_TEST_EXPECT_MSG_EQ(control.n2Failures.empty(), true,
                              "off: the far end sees what it did");
        // Multi-hop delivery is unaffected.
        NS_TEST_EXPECT_MSG_EQ(control.delivered, 30u, "off: every data packet arrives");
        NS_TEST_EXPECT_MSG_EQ(on.delivered, control.delivered, "on: the same delivery");
    }
};

/// A hybrid node: n0 -p2p- n1 -wifi- n2. n1 must stay silent on its ISL and
/// keep beaconing on the shared medium; a restart announces its ISL peer
/// again.
class IslHelloHybridTestCase : public TestCase {
public:
    IslHelloHybridTestCase()
        : TestCase("glue: a node with an ISL and a shared medium suppresses beacons on "
                   "the ISL only; restart re-announces link-state peers (#4)") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(3);
        PointToPointHelper p2p;
        p2p.SetChannelAttribute("Delay", StringValue("2ms"));
        p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
        NetDeviceContainer d01 = p2p.Install(nodes.Get(0), nodes.Get(1));
        NodeContainer air(nodes.Get(1), nodes.Get(2));
        NetDeviceContainer dw = AdhocWifi(air, 50.0);
        SwarmRoutingHelper helper("ns3::swarmtest::ToyProtocol");
        helper.Set("IslHelloSuppression", BooleanValue(true));
        helper.Install(nodes);
        helper.AssignStreams(nodes, 200);
        WifiHelper::AssignStreams(dw, 10);
        Ipv4AddressHelper a;
        a.SetBase("10.1.1.0", "255.255.255.0");
        a.Assign(d01);
        a.SetBase("10.2.0.0", "255.255.255.0");
        a.Assign(dw);
        Ptr<Node> n0 = nodes.Get(0);
        Ptr<Node> n1 = nodes.Get(1);
        Ptr<Node> n2 = nodes.Get(2);
        for (uint32_t i = 0; i < 3; ++i) {
            Toy(nodes.Get(i))->helloInterval = 0.25;
            Toy(nodes.Get(i))->beacon = true;
        }
        const uint32_t ifP2p = n1->GetObject<Ipv4>()->GetInterfaceForDevice(d01.Get(1));
        const uint32_t ifAir = n1->GetObject<Ipv4>()->GetInterfaceForDevice(dw.Get(0));
        const Ipv4Address n0a = Addr(n0, 1);
        const Ipv4Address n2w = Addr(n2, 1);
        bool p2pSuppressed = false;
        bool airSuppressed = true;
        bool n2LinkState = true;
        Simulator::Schedule(Seconds(1.0), [&]() {
            p2pSuppressed = Toy(n1)->BeaconSuppressed(ifP2p);
            airSuppressed = Toy(n1)->BeaconSuppressed(ifAir);
            n2LinkState = Toy(n1)->IsLinkStateNeighbor(n2w);
        });
        Simulator::Schedule(Seconds(3.0), [&]() { Toy(n1)->Crash(); });
        Simulator::Schedule(Seconds(3.5), [&]() { Toy(n1)->Start(); });
        Simulator::Stop(Seconds(5));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ(p2pSuppressed, true, "the ISL is silent");
        NS_TEST_EXPECT_MSG_EQ(airSuppressed, false, "the shared medium is not");
        NS_TEST_EXPECT_MSG_EQ(n2LinkState, false, "a Wi-Fi neighbour is hello's business");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->controlLog.empty(), true, "n0 hears no hello over p2p");
        uint32_t fromN1 = 0;
        uint32_t fromN1AfterRestart = 0;
        std::ostringstream n1w;
        n1w << Addr(n1, 2) << "->";
        for (const std::string& l : Toy(n2)->controlLog) {
            if (l.find(n1w.str()) != std::string::npos) {
                ++fromN1;
                fromN1AfterRestart += std::stod(l) > 3.5 ? 1 : 0;
            }
        }
        NS_TEST_EXPECT_MSG_GT(fromN1, 5u, "n2 hears n1's hellos over Wi-Fi");
        NS_TEST_EXPECT_MSG_GT(fromN1AfterRestart, 0u, "and again after n1 restarts");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->controlLog.empty(), false, "n1 hears n2's");
        uint32_t n0Ups = 0;
        for (const Ipv4Address& u : Toy(n1)->ups) {
            n0Ups += u == n0a ? 1 : 0;
        }
        NS_TEST_EXPECT_MSG_EQ(n0Ups, 2u, "n0 announced at start and again after restart");
        NS_TEST_EXPECT_MSG_EQ(Contains(Toy(n1)->ups, n2w), false, "never the Wi-Fi peer");
        Simulator::Destroy();
    }
};

/// A monitor aggregated after the protocol initialised is connected then:
/// its peers are announced and its link changes reach the protocol (#4).
class IslLateMonitorTestCase : public TestCase {
public:
    IslLateMonitorTestCase()
        : TestCase("glue: an IslLinkMonitor aggregated after initialisation is connected (#4)") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(2);
        PointToPointHelper p2p;
        NetDeviceContainer d = p2p.Install(nodes.Get(0), nodes.Get(1));
        SwarmRoutingHelper routing("ns3::swarmtest::ToyProtocol");
        routing.Set("IslHelloSuppression", BooleanValue(true));
        InternetStackHelper stack;
        stack.SetRoutingHelper(routing);
        stack.Install(nodes);  // no monitors
        Ipv4AddressHelper a;
        a.SetBase("10.1.1.0", "255.255.255.0");
        a.Assign(d);
        Ptr<Node> n0 = nodes.Get(0);
        const Ipv4Address n1a = Addr(nodes.Get(1), 1);
        bool suppressed = false;
        Simulator::Schedule(Seconds(1.0), [&]() {
            CreateObject<IslLinkMonitor>()->Install(n0);
            suppressed = Toy(n0)->BeaconSuppressed(1);
        });
        Simulator::Schedule(Seconds(2.0), [&]() { n0->GetObject<Ipv4>()->SetDown(1); });
        Simulator::Stop(Seconds(3));
        Simulator::Run();
        NS_TEST_EXPECT_MSG_EQ(suppressed, true, "the late monitor makes the ISL authoritative");
        NS_TEST_EXPECT_MSG_EQ(Contains(Toy(n0)->ups, n1a), true, "and its peer is announced");
        NS_TEST_ASSERT_MSG_EQ(Toy(n0)->failures.size(), 1u, "its link change reaches the protocol");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->failures[0], n1a, "the peer");
        Simulator::Destroy();
    }
};

class WifiMonitorTestCase : public TestCase {
public:
    WifiMonitorTestCase()
        : TestCase("glue: WifiLinkMonitor backlog, service time, SINR, "
                   "retry-limit failure") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(2);
        NetDeviceContainer devs = AdhocWifi(nodes, 50.0);
        InternetStackHelper stack;  // static routing: the monitor needs no swarm protocol
        stack.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.1.0", "255.255.255.0");
        a.Assign(devs);
        WifiHelper::AssignStreams(devs, 10);
        stack.AssignStreams(nodes, 100);
        NS_TEST_ASSERT_MSG_EQ(SwarmMonitorHelper().Install(nodes), 2u, "one monitor per node");
        Ptr<Node> n0 = nodes.Get(0);
        Ptr<Node> n1 = nodes.Get(1);
        Ptr<WifiLinkMonitor> m0 = n0->GetObject<WifiLinkMonitor>();
        Ptr<WifiLinkMonitor> m1 = n1->GetObject<WifiLinkMonitor>();
        const Ipv4Address n0a = Addr(n0, 1);
        const Ipv4Address n1a = Addr(n1, 1);
        std::vector<Ipv4Address> failed;
        m0->TraceConnectWithoutContext("LinkFailure",
                                       MakeBoundCallback(
                                           +[](std::vector<Ipv4Address>* out, Ipv4Address ip,
                                               Mac48Address) { out->push_back(ip); },
                                           &failed));

        std::vector<std::string> rx;
        Ptr<Socket> sink = Sink(n1, &rx);
        Ptr<Socket> src = Source(n0);
        int backlog = -1;
        double service = -1.0;
        double sinr = std::nan("");
        double sinrUnknown = 0.0;
        Simulator::Schedule(Seconds(0.5), [&]() { SendUdp(src, n1a, 100); });  // ARP warm-up
        Simulator::Schedule(Seconds(1.0), [&]() {
            for (int i = 0; i < 30; ++i) {
                SendUdp(src, n1a, 1000);
            }
        });
        Simulator::Schedule(Seconds(1.0) + MicroSeconds(1),
                            [&]() { backlog = m0->macQueueLength(swarm::core::kInvalidAddress); });
        Simulator::Schedule(Seconds(2.0), [&]() {
            service = m0->macServiceTime(ToNodeAddress(n1a));
            sinr = m1->sinrDbFrom(ToNodeAddress(n0a));
            sinrUnknown = m1->sinrDbFrom(ToNodeAddress(Ipv4Address("10.9.9.9")));
        });
        Simulator::Schedule(Seconds(3.0), [&]() {
            n1->GetObject<MobilityModel>()->SetPosition(Vector(5000.0, 0.0, 0.0));
        });
        Simulator::Schedule(Seconds(3.1), [&]() { SendUdp(src, n1a, 100); });
        Simulator::Stop(Seconds(5));
        Simulator::Run();

        NS_TEST_EXPECT_MSG_EQ(rx.size(), 31u, "warm-up and burst delivered");
        NS_TEST_EXPECT_MSG_GT(backlog, 10, "the burst backs up the MAC queue");
        // 1000 B at 6 Mb/s is ~1.4 ms of payload; with preamble, backoff and
        // the ACK a frame takes ~2 ms.
        NS_TEST_EXPECT_MSG_GT(service, 1.4e-3, "service time includes the MAC overhead");
        NS_TEST_EXPECT_MSG_LT(service, 5e-3, "service time excludes queue wait");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(sinr), false, "n1 heard n0");
        NS_TEST_EXPECT_MSG_GT(sinr, 10.0, "50 m at 6 Mb/s is a good link");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(sinrUnknown), true, "never heard: NaN, not bad");
        NS_TEST_EXPECT_MSG_GT(m0->LinkFailures(), 0u, "the out-of-range unicast hit the limit");
        NS_TEST_ASSERT_MSG_EQ(failed.empty(), false, "LinkFailure fired");
        NS_TEST_EXPECT_MSG_EQ(failed[0], n1a, "resolved to the neighbour's IP through ARP");
        Simulator::Destroy();
    }
};

/// The MAC gives an undeliverable data packet back to the protocol with the
/// hop it came from, and ResendData sends it back there (ARA's backtracking,
/// #40): n0 -> n1 -> n2 on Wi-Fi, n2 walks away, n1 backtracks to n0.
class UndeliveredTestCase : public TestCase {
public:
    enum class Variant { Plain, Checksums, MacFragments };

    explicit UndeliveredTestCase(Variant v)
        : TestCase(std::string("glue: a packet the MAC gave up on comes back with its previous "
                               "hop") +
                   (v == Variant::Checksums      ? ", checksums on"
                    : v == Variant::MacFragments ? "; a MAC fragment does not"
                                                 : "")),
          m_variant(v) {}

private:
    Variant m_variant;

    void DoRun() override {
        SwarmCoreSeed(1);
        // A resent header must carry a valid checksum, or n0 drops it (#95).
        GlobalValue::Bind("ChecksumEnabled", BooleanValue(m_variant == Variant::Checksums));
        // 300 B: the 1000 B data packet leaves n1 in fragments, the warm-up does not.
        Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold",
                           UintegerValue(m_variant == Variant::MacFragments ? 300 : 65535));
        const uint32_t size = m_variant == Variant::MacFragments ? 1000 : 100;
        NodeContainer nodes;
        nodes.Create(3);
        NetDeviceContainer devs = AdhocWifi(nodes, 50.0);
        SwarmRoutingHelper("ns3::swarmtest::ToyProtocol").Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.1.0", "255.255.255.0");
        a.Assign(devs);
        WifiHelper::AssignStreams(devs, 10);
        Ptr<Node> n0 = nodes.Get(0);
        Ptr<Node> n1 = nodes.Get(1);
        Ptr<Node> n2 = nodes.Get(2);
        const Ipv4Address n0a = Addr(n0, 1);
        const Ipv4Address n1a = Addr(n1, 1);
        const Ipv4Address n2a = Addr(n2, 1);
        Toy(n0)->routes[n2a] = n1a;
        Toy(n1)->routes[n2a] = n2a;
        Toy(n1)->backtrack = true;
        Toy(n0)->noRouteFrom = n1a;  // the packet n1 sends back stops at n0

        std::vector<std::string> rx;
        Ptr<Socket> sink = Sink(n2, &rx);
        Ptr<Socket> src = Source(n0);
        Simulator::Schedule(Seconds(0.5), [&]() { SendUdp(src, n2a, 100); });  // ARP warm-up
        Simulator::Schedule(Seconds(2.0), [&]() {
            n2->GetObject<MobilityModel>()->SetPosition(Vector(5000.0, 0.0, 0.0));
        });
        Simulator::Schedule(Seconds(2.1), [&]() { SendUdp(src, n2a, size); });
        Simulator::Stop(Seconds(4));
        Simulator::Run();
        GlobalValue::Bind("ChecksumEnabled", BooleanValue(false));
        Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold",
                           UintegerValue(65535));

        NS_TEST_EXPECT_MSG_EQ(rx.size(), 1u, "the warm-up arrived, the second packet could not");
        if (m_variant == Variant::MacFragments) {
            NS_TEST_EXPECT_MSG_EQ(Toy(n1)->failures.empty(), false, "the link failure is reported");
            NS_TEST_EXPECT_MSG_EQ(Toy(n1)->undelivered.size(), 0u,
                                  "a MAC fragment is no whole datagram: not handed back");
            Simulator::Destroy();
            return;
        }
        NS_TEST_ASSERT_MSG_EQ(Toy(n1)->undelivered.size(), 1u, "n1's MAC gave the packet back");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->undelivered[0].first, n2a, "toward n2");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->undelivered[0].second, n0a, "with n1's previous hop");
        NS_TEST_ASSERT_MSG_EQ(Toy(n1)->resent.size(), 1u, "n1 backtracked once");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->resent[0], true, "ResendData sent it");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->undelivered.size(), 0u, "n0's frames all arrived");
        bool backFromN1 = false;  // n0 originated both packets: only a returned one is listed
        for (const Ipv4Address& from : Toy(n0)->dataFrom) {
            backFromN1 |= from == n1a;
        }
        NS_TEST_EXPECT_MSG_EQ(backFromN1, true, "the data packet came back to n0 from n1");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->GetDeferredQueue()->Stats().enqueued, 1u,
                              "n0 had no other route for it and held it");
        Simulator::Destroy();
    }
};

/// OnTxFailure (#128): one call per retry-limit drop, for a data frame with
/// its datagram and for a control frame without; the default keeps the
/// OnLinkFailure + OnDataUndelivered sequence, an override replaces it.
class TxFailureTestCase : public TestCase {
public:
    enum class Variant { Default, Overridden, CrashInLinkFailure };

    explicit TxFailureTestCase(Variant v)
        : TestCase(std::string("glue: one OnTxFailure per retry-limit drop, data or control") +
                   (v == Variant::Default      ? ", default"
                    : v == Variant::Overridden ? ", overridden"
                                               : "; no data hook once OnLinkFailure took the "
                                                 "node down")),
          m_variant(v), m_txOnly(v == Variant::Overridden) {}

private:
    Variant m_variant;
    bool m_txOnly;

    void DoRun() override {
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(3);
        NetDeviceContainer devs = AdhocWifi(nodes, 50.0);
        SwarmRoutingHelper("ns3::swarmtest::ToyProtocol").Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.1.0", "255.255.255.0");
        a.Assign(devs);
        WifiHelper::AssignStreams(devs, 10);
        Ptr<Node> n0 = nodes.Get(0);
        Ptr<Node> n1 = nodes.Get(1);
        Ptr<Node> n2 = nodes.Get(2);
        const Ipv4Address n1a = Addr(n1, 1);
        const Ipv4Address n2a = Addr(n2, 1);
        Toy(n0)->routes[n2a] = n1a;
        Toy(n1)->routes[n2a] = n2a;
        Toy(n1)->txOnly = m_txOnly;
        Toy(n1)->crashOnLinkFailure = m_variant == Variant::CrashInLinkFailure;

        std::vector<std::string> rx;
        Ptr<Socket> sink = Sink(n2, &rx);
        Ptr<Socket> src = Source(n0);
        Simulator::Schedule(Seconds(0.5), [&]() { SendUdp(src, n2a, 100); });  // ARP warm-up
        Simulator::Schedule(Seconds(2.0), [&]() {
            n2->GetObject<MobilityModel>()->SetPosition(Vector(5000.0, 0.0, 0.0));
        });
        Simulator::Schedule(Seconds(2.1), [&]() { SendUdp(src, n2a, 100); });            // data
        Simulator::Schedule(Seconds(3.0), [&]() { Toy(n1)->UnicastMessage(n2a, 20); });  // control
        Simulator::Stop(Seconds(4));
        Simulator::Run();

        const Mac48Address n2mac = Mac48Address::ConvertFrom(n2->GetDevice(0)->GetAddress());
        const std::vector<std::string> expected =
            m_txOnly ? std::vector<std::string>{"tx data 10.1.1.3", "tx control"}
                     : std::vector<std::string>{"tx data 10.1.1.3", "link", "undelivered",
                                                "tx control", "link"};
        std::string got;
        for (const auto& l : Toy(n1)->txLog) {
            got += l + "; ";
        }
        if (m_variant == Variant::CrashInLinkFailure) {
            // Down after the data drop: no OnDataUndelivered, and the control
            // message is never sent.
            NS_TEST_EXPECT_MSG_EQ(
                (Toy(n1)->txLog == std::vector<std::string>{"tx data 10.1.1.3", "link"}), true,
                "hook sequence: " + got);
            NS_TEST_EXPECT_MSG_EQ(Toy(n1)->undelivered.size(), 0u, "no data hook when down");
            Simulator::Destroy();
            return;
        }
        NS_TEST_EXPECT_MSG_EQ((Toy(n1)->txLog == expected), true, "hook sequence: " + got);
        NS_TEST_ASSERT_MSG_EQ(Toy(n1)->txMacs.size(), 2u, "one call per lost frame");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->txMacs[0], n2mac, "the receiver's MAC (data)");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->txMacs[1], n2mac, "the receiver's MAC (control)");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->failures.size(), m_txOnly ? 0u : 2u,
                              "OnLinkFailure only from the default");
        NS_TEST_EXPECT_MSG_EQ(Toy(n1)->undelivered.size(), m_txOnly ? 0u : 1u,
                              "OnDataUndelivered only from the default, data only");
        NS_TEST_EXPECT_MSG_EQ(Toy(n0)->txLog.empty(), true, "n0's frames all arrived");
        Simulator::Destroy();
    }
};

// --- EnergyMetricHook
// ----------------------------------------------------------------

class EnergyHookTestCase : public TestCase {
public:
    EnergyHookTestCase()
        : TestCase("glue: EnergyMetricHook residual fraction from the source's "
                   "trace") {}

private:
    void DoRun() override {
        NodeContainer nodes;
        nodes.Create(2);
        BasicEnergySourceHelper energy;
        energy.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(10.0));
        energy::EnergySourceContainer sources = energy.Install(nodes.Get(0));
        Ptr<energy::EnergySource> source = sources.Get(0);
        Ptr<energy::SimpleDeviceEnergyModel> load = CreateObject<energy::SimpleDeviceEnergyModel>();
        load->SetNode(nodes.Get(0));
        load->SetEnergySource(source);
        source->AppendDeviceEnergyModel(load);

        NS_TEST_ASSERT_MSG_EQ(SwarmMonitorHelper().Install(nodes), 1u,
                              "a hook on the node with a source only");
        NS_TEST_ASSERT_MSG_EQ(SwarmMonitorHelper().Install(nodes), 0u, "idempotent");
        Ptr<EnergyMetricHook> hook = nodes.Get(0)->GetObject<EnergyMetricHook>();
        NS_TEST_ASSERT_MSG_EQ(static_cast<bool>(hook), true, "aggregated to its node");
        NS_TEST_EXPECT_MSG_EQ(static_cast<bool>(nodes.Get(1)->GetObject<EnergyMetricHook>()), false,
                              "no source, no hook");
        Ptr<EnergyMetricHook> bare = CreateObject<EnergyMetricHook>();
        NS_TEST_EXPECT_MSG_EQ(bare->Install(nodes.Get(1)), false, "Install(node) without source");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(bare->residualEnergyFraction()), true, "no source: NaN");
        NS_TEST_EXPECT_MSG_EQ_TOL(hook->residualEnergyFraction(), 1.0, 1e-12, "starts full");

        double at25 = -1.0;
        // 1 A at the source's 3 V: 3 W, so 6 J of 10 are gone after 2 s. The
        // source updates every second; the hook reads that, never the source.
        Simulator::Schedule(Seconds(0.0), [&]() { load->SetCurrentA(1.0); });
        Simulator::Schedule(Seconds(2.5), [&]() { at25 = hook->residualEnergyFraction(); });
        Simulator::Stop(Seconds(2.6));
        Simulator::Run();
        NS_TEST_EXPECT_MSG_EQ_TOL(at25, 0.4, 1e-9, "the value of the last periodic update");
        Simulator::Destroy();
        bare->Dispose();
    }
};

// --- determinism
// ---------------------------------------------------------------------

/// Three wifi nodes in range of each other, the toy protocol with jittered
/// hellos and jittered discovery (both drawn from the Rng port), energy
/// sources on every node, and two flows to n2. Returns everything observable:
/// the data received and every control message, with times.
/// `observe` connects every observer trace source and reads the counters
/// mid-run; what it sees goes to `observed`, never into the returned output.
std::string RunDeterminismScenario(uint32_t seed, bool monitors, bool observe = false,
                                   std::string* observed = nullptr) {
    SwarmCoreSeed(seed);
    NodeContainer nodes;
    nodes.Create(3);
    NetDeviceContainer devs = AdhocWifi(nodes, 20.0);
    BasicEnergySourceHelper energy;
    energy.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(100.0));
    energy.Install(nodes);
    SwarmRoutingHelper toy("ns3::swarmtest::ToyProtocol");
    InternetStackHelper stack;
    stack.SetRoutingHelper(toy);
    stack.Install(nodes);
    if (monitors) {
        SwarmMonitorHelper().Install(nodes);
    }
    Ipv4AddressHelper a;
    a.SetBase("10.1.1.0", "255.255.255.0");
    a.Assign(devs);
    WifiHelper::AssignStreams(devs, 10);
    stack.AssignStreams(nodes, 100);
    toy.AssignStreams(nodes, 200);
    const Ipv4Address n2a = Addr(nodes.Get(2), 1);
    for (uint32_t i = 0; i < 3; ++i) {
        Ptr<ToyProtocol> p = Toy(nodes.Get(i));
        p->helloInterval = 0.5;
        p->discoveryJitter = 0.2;
        p->pending[n2a] = n2a;
    }
    std::vector<std::string> rx;
    Ptr<Socket> sink = Sink(nodes.Get(2), &rx);
    Ptr<Socket> s0 = Source(nodes.Get(0));
    Ptr<Socket> s1 = Source(nodes.Get(1));
    for (int i = 0; i < 20; ++i) {
        Simulator::Schedule(Seconds(1.0 + 0.1 * i), [s0, n2a]() { SendUdp(s0, n2a, 512); });
        Simulator::Schedule(Seconds(1.05 + 0.1 * i), [s1, n2a]() { SendUdp(s1, n2a, 256); });
    }
    std::ostringstream probe;
    if (monitors) {
        // Read every port mid-run, as a protocol would: reading must not
        // change the run either.
        for (int t = 1; t <= 4; ++t) {
            Simulator::Schedule(Seconds(t), [&nodes, &probe, n2a]() {
                for (uint32_t i = 0; i < 3; ++i) {
                    Ptr<ToyProtocol> p = Toy(nodes.Get(i));
                    probe << p->LinkState().macQueueLength(swarm::core::kInvalidAddress)
                          << p->LinkState().macServiceTime(ToNodeAddress(n2a))
                          << p->LinkQuality().sinrDbFrom(ToNodeAddress(n2a))
                          << p->Energy().residualEnergyFraction();
                }
            });
        }
    }
    std::vector<std::string> traces;
    if (observe) {
        for (uint32_t i = 0; i < 3; ++i) {
            ConnectObserverTraces(Toy(nodes.Get(i)), &traces);
        }
        for (int t = 1; t <= 4; ++t) {
            Simulator::Schedule(Seconds(t), [&nodes, &traces]() {
                const SwarmRoutingStats s = SwarmRoutingHelper::CollectStats(nodes);
                traces.push_back("stats " + std::to_string(s.aggregate.routeAdds) + " " +
                                 std::to_string(s.aggregate.peakStateEntries));
            });
        }
    }
    Simulator::Stop(Seconds(5));
    Simulator::Run();
    if (observed) {
        for (const auto& l : traces) {
            *observed += l + "\n";
        }
    }
    std::ostringstream out;
    for (const auto& l : rx) {
        out << "rx " << l << "\n";
    }
    for (uint32_t i = 0; i < 3; ++i) {
        for (const auto& l : Toy(nodes.Get(i))->controlLog) {
            out << "ctl" << i << " " << l << "\n";
        }
    }
    Simulator::Destroy();
    return out.str();
}

class DeterminismTestCase : public TestCase {
public:
    DeterminismTestCase()
        : TestCase("glue: same seed, byte-identical output with and without the "
                   "monitors and the "
                   "observer") {}

private:
    void DoRun() override {
        const std::string without = RunDeterminismScenario(1, false);
        const std::string with = RunDeterminismScenario(1, true);
        const std::string again = RunDeterminismScenario(1, true);
        const std::string other = RunDeterminismScenario(2, true);
        std::string observed;
        std::string observedAgain;
        const std::string withObserver = RunDeterminismScenario(1, true, true, &observed);
        RunDeterminismScenario(1, true, true, &observedAgain);
        std::istringstream lines(without);
        uint32_t delivered = 0;
        for (std::string l; std::getline(lines, l);) {
            delivered += l.rfind("rx ", 0) == 0 ? 1 : 0;
        }
        // Of 40, the packets each source held during discovery leave in one
        // burst, and ARP's pending queue (3 packets) keeps only the first 3.
        NS_TEST_ASSERT_MSG_GT(delivered, 30u, "both flows deliver");
        NS_TEST_EXPECT_MSG_EQ(with, without, "monitors attached: identical output");
        NS_TEST_EXPECT_MSG_EQ(again, with, "same seed: identical output");
        NS_TEST_EXPECT_MSG_NE(other, with, "another seed: other output (the log sees the seed)");
        NS_TEST_EXPECT_MSG_EQ(withObserver, with, "observer traces and counters read: identical");
        NS_TEST_EXPECT_MSG_NE(observed.find("route "), std::string::npos, "the observer saw it");
        NS_TEST_EXPECT_MSG_EQ(observedAgain, observed, "same seed: identical observer output");
    }
};

}  // namespace

std::vector<TestCase*> MakeGlueTestCases() {
    return {new PortsTestCase,
            new DeferredQueueTestCase,
            new DeferredQueueOrderTestCase,
            new PortCacheTestCase,
            new CancellableTimerTestCase,
            new WireBufferTestCase,
            new RoutingBaseTestCase,
            new DeferFromAdapterTestCase,
            new IslMonitorTestCase,
            new IslHelloSuppressionTestCase,
            new IslHelloHybridTestCase,
            new IslLateMonitorTestCase,
            new WifiMonitorTestCase,
            new UndeliveredTestCase(UndeliveredTestCase::Variant::Plain),
            new UndeliveredTestCase(UndeliveredTestCase::Variant::Checksums),
            new UndeliveredTestCase(UndeliveredTestCase::Variant::MacFragments),
            new TxFailureTestCase(TxFailureTestCase::Variant::Default),
            new TxFailureTestCase(TxFailureTestCase::Variant::Overridden),
            new TxFailureTestCase(TxFailureTestCase::Variant::CrashInLinkFailure),
            new EnergyHookTestCase,
            new ObserverTestCase,
            new LifecycleTestCase,
            new LifecycleHelperTestCase,
            new DeterminismTestCase};
}

}  // namespace ns3::swarmtest
