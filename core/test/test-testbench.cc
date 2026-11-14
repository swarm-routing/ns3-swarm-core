// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The discrete-event testbench (#15, core/testbench/swarm-testbench.h): its
// own unit tests (event queue, topology, ideal MAC, ports, bounds,
// determinism), the README example, and the reference trend test: a toy
// reactive protocol on a 6-node line with one link break, where local repair
// costs less control than re-flooding from the source and delivers at least
// as much. The trend idea is swarm-routing/AntHocNet#155's (there, planned in
// core/tests/); the toy protocol is written for this test.
#include "../swarm-dedup-history.h"
#include "../testbench/swarm-testbench.h"
#include "swarm-core-tests.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace swarm::core::test {

namespace {

namespace tb = swarm::core::testbench;

// --- the README / header example, verbatim in shape -------------------------------

struct Hello {
    int from;
};

class FloodNode : public tb::INodeAdapter<Hello> {
public:
    explicit FloodNode(tb::NodePorts<Hello>& p) : p_(p) {}
    void start() override {
        if (p_.address() == 0) {
            heard_ = true;
            p_.broadcast(Hello{0});
        }
    }
    void receive(NodeAddress, const Hello& h) override {
        if (!heard_) {
            heard_ = true;
            p_.broadcast(h);
        }
    }
    bool heard_ = false;

private:
    tb::NodePorts<Hello>& p_;
};

// --- a toy reactive protocol: flood discovery, target-only replies ----------------

enum ToyKind : std::uint8_t { kRreq = 1, kRrep = 2, kData = 3, kRerr = 4 };

struct ToyMsg {
    std::uint8_t kind = kData;
    NodeAddress origin = kInvalidAddress;  ///< RREQ/RREP: the asker; DATA/RERR: the source
    NodeAddress target = kInvalidAddress;  ///< the destination looked for or sent to
    std::uint32_t seq = 0;
    std::uint8_t ttl = 0;
};

enum class Repair { Local, Flood };

/// 80 data packets from node 0 to node 5, one every 0.1 s from 1 s. A route is found by
/// flooding a request the destination answers along the reverse path. When a
/// unicast finds no link: with Repair::Local the node holds the data and asks
/// within kRepairTtl hops itself; with Repair::Flood it drops the data and
/// sends an error to the source, which floods again. Requests are retried
/// every 0.5 s while data waits; rebroadcasts are jittered from the node's
/// IRng, so the trace depends on the seed.
class ToyRouter : public tb::INodeAdapter<ToyMsg> {
public:
    static constexpr NodeAddress kSource = 0;
    static constexpr NodeAddress kDest = 5;
    static constexpr std::uint8_t kFloodTtl = 255;
    static constexpr std::uint8_t kRepairTtl = 2;
    static constexpr std::size_t kBufferCap = 64;
    static constexpr std::uint32_t kPackets = 80;  ///< 1.0 s to 8.9 s

    ToyRouter(tb::NodePorts<ToyMsg>& p, Repair mode) : p_(p), mode_(mode), seen_(64) {}

    void start() override {
        if (p_.address() == kSource) p_.timers().schedule(1.0, [this]() { tick(); });
    }

    void receive(NodeAddress from, const ToyMsg& m) override {
        switch (m.kind) {
        case kRreq:
            if (m.origin == p_.address()) return;
            if (!seen_.record(m.origin, m.seq)) return;
            routes_[m.origin] = from;  // the reverse path, from the first copy only
            if (m.target == p_.address()) {
                p_.unicast(from, ToyMsg{kRrep, m.origin, m.target, m.seq, 0});
            } else if (m.ttl > 1) {
                ToyMsg fwd = m;
                --fwd.ttl;
                p_.timers().schedule(0.005 * p_.rng().uniform(),
                                     [this, fwd]() { p_.broadcast(fwd); });
            }
            return;
        case kRrep:
            routes_[m.target] = from;
            if (m.origin == p_.address()) {
                discovering_ = false;
                flush();
            } else if (auto it = routes_.find(m.origin); it != routes_.end()) {
                p_.unicast(it->second, m);
            }
            return;
        case kData:
            if (m.target == p_.address()) {
                ++delivered;
            } else {
                forward(m);
            }
            return;
        case kRerr:
            routes_.erase(m.target);
            if (m.origin != p_.address()) {
                if (auto it = routes_.find(m.origin); it != routes_.end())
                    p_.unicast(it->second, m);
            }
            return;
        default:
            return;
        }
    }

    void undelivered(NodeAddress to, const ToyMsg& m) override {
        for (auto it = routes_.begin(); it != routes_.end();) {
            it = it->second == to ? routes_.erase(it) : std::next(it);
        }
        if (m.kind != kData) return;
        if (mode_ == Repair::Local || p_.address() == kSource) {
            hold(m);
            return;
        }
        // Flood: tell the source, drop the data.
        if (auto it = routes_.find(m.origin); it != routes_.end()) {
            p_.unicast(it->second, ToyMsg{kRerr, m.origin, m.target, 0, 0});
        }
    }

    std::uint32_t delivered = 0;
    std::uint32_t rounds = 0;  ///< discovery rounds this node started

private:
    void tick() {
        forward(ToyMsg{kData, kSource, kDest, ++dataSeq_, 0});
        if (dataSeq_ < kPackets) p_.timers().schedule(0.1, [this]() { tick(); });
    }

    void forward(const ToyMsg& m) {
        if (auto it = routes_.find(m.target); it != routes_.end()) {
            p_.unicast(it->second, m);
        } else {
            hold(m);
        }
    }

    void hold(const ToyMsg& m) {
        if (buffer_.size() < kBufferCap) buffer_.push_back(m);  // bounded (golden rule 7)
        if (!discovering_) discover(m.target);
    }

    void discover(NodeAddress target) {
        discovering_ = true;
        ++rounds;
        const std::uint8_t ttl = p_.address() == kSource ? kFloodTtl : kRepairTtl;
        const ToyMsg req{kRreq, p_.address(), target, ++reqSeq_, ttl};
        seen_.record(req.origin, req.seq);
        p_.broadcast(req);
        p_.timers().schedule(0.5, [this, target]() {
            if (discovering_ && routes_.count(target) == 0 && !buffer_.empty()) {
                discover(target);
            } else {
                discovering_ = false;
            }
        });
    }

    void flush() {
        std::deque<ToyMsg> held;
        held.swap(buffer_);
        for (const ToyMsg& m : held)
            forward(m);
    }

    tb::NodePorts<ToyMsg>& p_;
    Repair mode_;
    DedupHistory seen_;
    std::map<NodeAddress, NodeAddress> routes_;
    std::deque<ToyMsg> buffer_;
    bool discovering_ = false;
    std::uint32_t reqSeq_ = 0;
    std::uint32_t dataSeq_ = 0;
};

struct TrendRun {
    std::uint64_t controlAfterBreak = 0;  ///< RREQ + RREP + RERR sent in [5, 10)
    std::uint64_t rreqAfterBreak = 0;
    std::uint32_t delivered = 0;
    std::uint32_t deliveredAfterHeal = 0;
    std::uint32_t repairRounds = 0;  ///< discovery rounds started after the break
    std::uint64_t digest = 0;
    std::vector<tb::TraceRecord> trace;
    std::uint64_t traceDropped = 0;
    std::uint64_t refused = 0;
};

std::uint64_t Control(const tb::Network<ToyMsg>& net) {
    return net.sent(kRreq) + net.sent(kRrep) + net.sent(kRerr);
}

/// 0 - 1 - 2 - 3 - 4 - 5; the link 3 - 4 breaks at 5 s and comes back at 6 s.
TrendRun RunTrend(Repair mode, std::uint64_t seed, std::size_t maxTrace = std::size_t{1} << 16) {
    tb::NetworkConfig cfg;
    cfg.seed = seed;
    cfg.maxTrace = maxTrace;
    tb::Network<ToyMsg> net(tb::Topology::line(6), cfg, [](const ToyMsg& m) { return m.kind; });
    net.install([mode](tb::NodePorts<ToyMsg>& p) { return std::make_unique<ToyRouter>(p, mode); });
    net.linkAt(5.0, 3, 4, false);
    net.linkAt(6.0, 3, 4, true);
    TrendRun r;
    std::uint64_t control5 = 0;
    std::uint64_t rreq5 = 0;
    std::uint32_t delivered6 = 0;
    std::uint32_t rounds5 = 0;
    auto rounds = [&net]() {
        std::uint32_t n = 0;
        for (NodeAddress a = 0; a < 6; ++a)
            n += net.nodeAs<ToyRouter>(a).rounds;
        return n;
    };
    net.at(5.0, [&]() {
        control5 = Control(net);
        rreq5 = net.sent(kRreq);
        rounds5 = rounds();
    });
    net.at(6.0, [&]() { delivered6 = net.nodeAs<ToyRouter>(ToyRouter::kDest).delivered; });
    net.run(10.0);
    r.controlAfterBreak = Control(net) - control5;
    r.rreqAfterBreak = net.sent(kRreq) - rreq5;
    r.delivered = net.nodeAs<ToyRouter>(ToyRouter::kDest).delivered;
    r.deliveredAfterHeal = r.delivered - delivered6;
    r.repairRounds = rounds() - rounds5;
    r.digest = net.digest();
    r.trace = net.trace();
    r.traceDropped = net.traceDropped();
    r.refused = net.events().refused();
    return r;
}

/// Records what reaches it.
struct Recorder : tb::INodeAdapter<int> {
    explicit Recorder(tb::NodePorts<int>& p) : ports(p) {}
    void receive(NodeAddress from, const int& m) override {
        got.push_back({from, m});
        at.push_back(ports.clock().now());
    }
    void undelivered(NodeAddress to, const int& m) override { bounced.push_back({to, m}); }
    void linkChanged(NodeAddress n, bool up) override { links.push_back({n, up ? 1 : 0}); }
    tb::NodePorts<int>& ports;
    std::vector<std::pair<NodeAddress, int>> got;
    std::vector<Time> at;
    std::vector<std::pair<NodeAddress, int>> bounced;
    std::vector<std::pair<NodeAddress, int>> links;
};

}  // namespace

void TestTestbenchEventQueue(Checker& c) {
    tb::EventQueue q(4);
    std::vector<int> order;
    c.check(q.schedule(2.0, [&]() { order.push_back(2); }) != kNoTimer, "scheduled");
    q.schedule(1.0, [&]() { order.push_back(1); });
    const TimerHandle doomed = q.schedule(1.0, [&]() { order.push_back(99); });
    q.schedule(1.0, [&]() { order.push_back(3); });  // same time: after the first
    c.check(q.schedule(5.0, [] {}) == kNoTimer, "a fifth pending event is refused");
    c.check(q.refused() == 1, "and counted");
    c.check(q.cancel(doomed), "cancel a pending event");
    c.check(!q.cancel(doomed), "twice is a no-op");
    c.check(!q.cancel(kNoTimer), "kNoTimer is a no-op");
    c.check(q.runUntil(1.5) == 2, "two events due by 1.5");
    c.check(q.now() == 1.5, "the clock reads the horizon");
    c.check((order == std::vector<int>{1, 3}), "time order, FIFO among equals, cancelled skipped");
    q.schedule(-1.0, [&]() { order.push_back(4); });  // negative delay: now
    c.check(q.runUntil(10.0, 1) == 1, "the event limit stops the run");
    c.check(order.back() == 4 && q.now() == 1.5, "the past runs now; limit leaves the clock");
    q.runUntil(10.0);
    c.check((order == std::vector<int>{1, 3, 4, 2}), "the rest");
    c.check(q.executed() == 4 && q.pending() == 0, "executed, nothing pending");
    // A self-rescheduling event is bounded by the run horizon.
    tb::EventQueue loop;
    std::function<void()> again = [&]() { loop.schedule(0.0, again); };
    loop.schedule(0.0, again);
    c.check(loop.runUntil(1.0, 1000) == 1000, "a zero-delay storm stops at the event limit");
}

void TestTestbenchTopology(Checker& c) {
    tb::Topology t = tb::Topology::line(4);
    c.check(t.size() == 4, "four nodes");
    c.check((t.neighbors(1) == std::vector<NodeAddress>{0, 2}), "ascending neighbours");
    c.check(t.linked(2, 3) && !t.linked(0, 2), "line links");
    t.connect(3, 0);
    t.connect(0, 0);  // no self link
    t.connect(0, 9);  // out of range: ignored
    c.check((t.neighbors(0) == std::vector<NodeAddress>{1, 3}), "a ring now");
    const auto cut = t.partition({0, 1});
    c.check((cut == std::vector<tb::Topology::Link>{{0, 3}, {1, 2}}), "the links across the cut");
    c.check(t.neighbors(0) == std::vector<NodeAddress>{1} &&
                t.neighbors(2) == std::vector<NodeAddress>{3},
            "two halves");
    t.connect(cut);
    c.check(t.linked(0, 3) && t.linked(1, 2), "healed");
    c.check(t.neighbors(-1).empty(), "an invalid address has no neighbours");
}

void TestTestbenchMac(Checker& c) {
    tb::NetworkConfig cfg;
    cfg.macDelay = 0.25;
    tb::Network<int> net(tb::Topology::line(3), cfg);
    net.install([](tb::NodePorts<int>& p) { return std::make_unique<Recorder>(p); });
    auto& n0 = net.nodeAs<Recorder>(0);
    auto& n1 = net.nodeAs<Recorder>(1);
    auto& n2 = net.nodeAs<Recorder>(2);
    net.at(1.0, [&]() { net.ports(1).broadcast(7); });
    net.at(2.0, [&]() { net.ports(0).unicast(1, 8); });
    net.at(3.0, [&]() { net.ports(0).unicast(2, 9); });  // not a neighbour
    net.linkAt(4.0, 1, 2, false);
    net.at(4.5, [&]() { net.ports(1).broadcast(10); });
    net.at(5.0, [&]() { net.setLoss(0, 1, 1.0); });
    net.at(5.5, [&]() { net.ports(0).unicast(1, 11); });
    net.run(10.0);
    c.check((n0.got == std::vector<std::pair<NodeAddress, int>>{{1, 7}, {1, 10}}), "n0 hears n1");
    c.check((n2.got == std::vector<std::pair<NodeAddress, int>>{{1, 7}}), "n2 only before the cut");
    c.check((n1.got == std::vector<std::pair<NodeAddress, int>>{{0, 8}}),
            "unicast; the lossy one lost");
    c.check(n0.at.size() == 2 && n0.at[0] == 1.25, "delivered after macDelay");
    c.check((n0.bounced == std::vector<std::pair<NodeAddress, int>>{{2, 9}}),
            "no link: undelivered");
    c.check((n1.links == std::vector<std::pair<NodeAddress, int>>{{2, 0}}),
            "both ends told of the cut");
    c.check((n2.links == std::vector<std::pair<NodeAddress, int>>{{1, 0}}), "both ends");
    c.check(net.sentTotal() == 5 && net.sentBy(0) == 3, "sends counted, a broadcast once");
    c.check(net.deliveredTotal() == 4 && net.undeliveredTotal() == 1 && net.lostTotal() == 1,
            "delivered, undelivered, lost");
    // The node's ports.
    auto& p2 = net.ports(2);
    c.check(p2.neighbors().empty() && &p2.neighborProvider() == &p2, "INeighborProvider: cut off");
    c.check(p2.clock().now() == 10.0, "IClock reads the bench's time");
    bool ran = false;
    const TimerHandle h = p2.timers().scheduleCancellable(1.0, [&]() { ran = true; });
    p2.timers().cancel(h);
    net.run(20.0);
    c.check(!ran, "ITimerScheduler: cancellable");
    bool threw = false;
    try {
        net.install([](tb::NodePorts<int>& p) { return std::make_unique<Recorder>(p); });
    } catch (const std::logic_error&) {
        threw = true;
    }
    c.check(threw, "a second install is refused");
    c.check(&net.nodeAs<Recorder>(0) == &n0, "and the adapters stay");
}

void TestTestbenchExample(Checker& c) {
    tb::NetworkConfig cfg;
    cfg.seed = 1;
    tb::Network<Hello> net(tb::Topology::line(6), cfg);
    net.install([](tb::NodePorts<Hello>& p) { return std::make_unique<FloodNode>(p); });
    net.linkAt(0.5, 2, 3, false);
    net.run(1.0);
    c.check(net.sentTotal() == 6, "the flood: one broadcast per node");
    c.check(net.nodeAs<FloodNode>(5).heard_, "it reached the far end before the break");
}

void TestTestbenchDeterminism(Checker& c) {
    const TrendRun a = RunTrend(Repair::Local, 1);
    const TrendRun b = RunTrend(Repair::Local, 1);
    const TrendRun other = RunTrend(Repair::Local, 2);
    c.check(a.digest == b.digest && a.trace == b.trace, "same seed: identical trace and digest");
    c.check(other.digest != a.digest, "another seed: another trace (jitter draws from IRng)");
    const TrendRun small = RunTrend(Repair::Local, 1, 10);
    c.check(small.trace.size() == 10 && small.traceDropped == a.trace.size() - 10,
            "the trace is bounded, the rest counted");
    c.check(small.digest == a.digest, "the digest sees every record");
    c.check(a.refused == 0, "no event refused");
}

void TestTestbenchLocalRepairTrend(Checker& c) {
    const TrendRun local = RunTrend(Repair::Local, 1);
    const TrendRun flood = RunTrend(Repair::Flood, 1);
    c.check(local.deliveredAfterHeal > 0 && flood.deliveredAfterHeal > 0, "both recover");
    c.check(local.delivered >= flood.delivered, "local repair delivers at least as much");
    c.check(local.delivered == ToyRouter::kPackets,
            "local repair loses nothing: the relay holds the data");
    c.check(local.controlAfterBreak < flood.controlAfterBreak, "local repair costs less control");
    c.check(2 * local.controlAfterBreak <= flood.controlAfterBreak + 2,
            "about half of re-flooding on this line");
    // Bounded: a TTL-2 request on a line is rebroadcast by at most the 2 nodes
    // one hop from the repairer, so a round costs at most 3 requests.
    c.check(local.repairRounds > 0, "repair ran");
    c.check(local.rreqAfterBreak <= 3 * local.repairRounds, "each round is bounded by its TTL");
}

}  // namespace swarm::core::test
