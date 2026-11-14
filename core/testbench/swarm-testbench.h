// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// A discrete-event testbench for protocol cores (#15): the ports a core takes
// (IClock, IRng, ITimerScheduler, INeighborProvider) driven by one event queue
// over N nodes, a scripted topology and an ideal MAC, so a protocol repository
// can assert algorithmic trends in its plain core tests, with no simulator:
// local repair beats re-flooding, storms stay bounded on a partition,
// evaporation forgets stale routes. When such a test fails, the algorithm is
// wrong, not the PHY or the harness (AntHocNet's #19/#43 vs #51 confusion,
// the motivation of swarm-routing/AntHocNet#64 and #155 there).
//
// Header-only, the C++ standard library and core/ only (golden rule 10), so a
// protocol that fetches core/ (README "Use core/ from another CMake project")
// includes it with no source list to keep: #include "testbench/swarm-testbench.h".
//
// - EventQueue: time-ordered events, FIFO among equal times, cancellable,
//   bounded (a schedule past maxPending is refused and counted).
// - Rng: SplitMix64 behind IRng; every node and the channel get their own
//   stream derived from one seed.
// - Topology: nodes 0..N-1 (a node's NodeAddress is its index), undirected
//   links, line(), partition().
// - Network<Msg>: the ideal MAC (broadcast to every current neighbour and
//   unicast, each delivered after macDelay; no loss unless scripted with
//   setLoss; a unicast to a node that is not a neighbour comes back through
//   INodeAdapter::undelivered, as a MAC retry limit would), scripted link
//   changes (linkAt), counters per message kind and node, and a bounded trace
//   with a digest of every record, so two runs compare byte for byte.
//
// Determinism (org rule 5): no wall clock, no libc rand(), ordered containers
// only; the same seed and script give the same trace and digest (tested).
//
// The adapter a protocol writes once: derive from INodeAdapter<Msg>, take the
// NodePorts<Msg>& the factory hands you, give its ports to your core, and turn
// your core's decisions into ports.broadcast / ports.unicast:
//
//     struct Hello { int from; };
//     class FloodNode : public tb::INodeAdapter<Hello> {
//     public:
//         explicit FloodNode(tb::NodePorts<Hello>& p) : p_(p) {}
//         void start() override {
//             // Your core gets p_.clock(), p_.rng(), p_.timers(), p_ (neighbours).
//             if (p_.address() == 0) { heard_ = true; p_.broadcast(Hello{0}); }
//         }
//         void receive(swarm::core::NodeAddress, const Hello& h) override {
//             if (!heard_) { heard_ = true; p_.broadcast(h); }  // rebroadcast once
//         }
//         bool heard_ = false;
//     private:
//         tb::NodePorts<Hello>& p_;
//     };
//
//     tb::Network<Hello> net(tb::Topology::line(6), {.seed = 1});
//     net.install([](tb::NodePorts<Hello>& p) { return std::make_unique<FloodNode>(p); });
//     net.linkAt(0.5, 2, 3, false);               // a scripted break
//     net.run(1.0);
//     check(net.sentTotal() == 6);               // a trend, not a PHY artefact
//     check(net.nodeAs<FloodNode>(5).heard_);
//
// (namespace tb = swarm::core::testbench.) core/test/test-testbench.cc has a
// full reference: reactive discovery with local repair vs re-flooding on a
// 6-node line with one link break.
#ifndef SWARM_CORE_TESTBENCH_H
#define SWARM_CORE_TESTBENCH_H

#include "../swarm-ports.h"
#include "../swarm-types.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace swarm::core::testbench {

// --- randomness -------------------------------------------------------------------

/// SplitMix64 behind IRng: fully specified, so a seed gives the same sequence
/// on every compiler and platform.
class Rng : public IRng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() override { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    /// n <= 0 breaks the IRng contract: 0, without drawing.
    int uniformInt(int n) override {
        if (n <= 0) return 0;
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }

    /// The seed of stream `stream` of run seed `seed`: node i draws from
    /// streamSeed(seed, i), the channel from streamSeed(seed, kChannelStream).
    static std::uint64_t streamSeed(std::uint64_t seed, std::uint64_t stream) {
        Rng mix(seed ^ (stream * 0xD1B54A32D192ED03ull));
        return mix.next();
    }
    static constexpr std::uint64_t kChannelStream = ~0ull;

private:
    std::uint64_t state_;
};

// --- the event queue --------------------------------------------------------------

/// Time-ordered events; equal times run in the order they were scheduled.
/// It is the bench's clock. Bounded: at most `maxPending` events wait at once;
/// one more is refused (not scheduled) and counted, so a runaway storm shows
/// up as refused() > 0 instead of exhausting memory (golden rule 7).
class EventQueue : public IClock {
public:
    static constexpr std::size_t kDefaultMaxPending = std::size_t{1} << 20;

    explicit EventQueue(std::size_t maxPending = kDefaultMaxPending) : maxPending_(maxPending) {}

    Time now() const override { return now_; }

    /// Runs `fn` `delay` seconds from now (a negative or NaN delay is 0).
    /// Returns its handle, or kNoTimer when the queue is full.
    TimerHandle schedule(Time delay, std::function<void()> fn) {
        return scheduleAt(now_ + (delay > 0.0 ? delay : 0.0), std::move(fn));
    }
    /// Runs `fn` at time `t` (now, if `t` is in the past or NaN).
    TimerHandle scheduleAt(Time t, std::function<void()> fn) {
        if (!(t >= now_)) t = now_;
        if (events_.size() >= maxPending_) {
            ++refused_;
            return kNoTimer;
        }
        const TimerHandle h = nextHandle_++;
        events_.emplace(Key{t, h}, std::move(fn));
        when_.emplace(h, t);
        return h;
    }
    /// Cancels an event that has not run; false for kNoTimer, a run or an
    /// already cancelled one.
    bool cancel(TimerHandle h) {
        auto it = when_.find(h);
        if (it == when_.end()) return false;
        events_.erase(Key{it->second, h});
        when_.erase(it);
        return true;
    }
    /// Runs the next event; false when there is none.
    bool step() {
        if (events_.empty()) return false;
        auto it = events_.begin();
        now_ = it->first.first;
        std::function<void()> fn = std::move(it->second);
        when_.erase(it->first.second);
        events_.erase(it);
        ++executed_;
        fn();
        return true;
    }
    /// Runs every event due at or before `until`, at most `maxEvents` of
    /// them; then the clock reads `until` unless the event limit stopped it.
    /// Returns the number run.
    std::uint64_t runUntil(Time until,
                           std::uint64_t maxEvents = std::numeric_limits<std::uint64_t>::max()) {
        std::uint64_t n = 0;
        while (!events_.empty() && events_.begin()->first.first <= until) {
            if (n == maxEvents) return n;
            step();
            ++n;
        }
        if (until > now_) now_ = until;
        return n;
    }

    std::size_t pending() const { return events_.size(); }
    std::uint64_t executed() const { return executed_; }
    std::uint64_t refused() const { return refused_; }

private:
    using Key = std::pair<Time, TimerHandle>;
    std::map<Key, std::function<void()>> events_;
    std::map<TimerHandle, Time> when_;
    Time now_ = 0.0;
    TimerHandle nextHandle_ = kNoTimer + 1;
    std::size_t maxPending_;
    std::uint64_t executed_ = 0;
    std::uint64_t refused_ = 0;
};

// --- the topology -----------------------------------------------------------------

/// Undirected links between nodes 0..N-1. A node's NodeAddress is its index;
/// an address out of range is ignored by every call.
class Topology {
public:
    using Link = std::pair<NodeAddress, NodeAddress>;

    explicit Topology(std::size_t nodes) : adj_(nodes) {}

    /// 0 - 1 - ... - (n-1).
    static Topology line(std::size_t n) {
        Topology t(n);
        for (std::size_t i = 1; i < n; ++i) {
            t.connect(static_cast<NodeAddress>(i - 1), static_cast<NodeAddress>(i));
        }
        return t;
    }

    std::size_t size() const { return adj_.size(); }
    bool valid(NodeAddress a) const { return a >= 0 && static_cast<std::size_t>(a) < adj_.size(); }

    void connect(NodeAddress a, NodeAddress b) {
        if (valid(a) && valid(b) && a != b) {
            adj_[static_cast<std::size_t>(a)].insert(b);
            adj_[static_cast<std::size_t>(b)].insert(a);
        }
    }
    void connect(const std::vector<Link>& links) {
        for (const Link& l : links)
            connect(l.first, l.second);
    }
    void disconnect(NodeAddress a, NodeAddress b) {
        if (valid(a) && valid(b)) {
            adj_[static_cast<std::size_t>(a)].erase(b);
            adj_[static_cast<std::size_t>(b)].erase(a);
        }
    }
    bool linked(NodeAddress a, NodeAddress b) const {
        return valid(a) && valid(b) && adj_[static_cast<std::size_t>(a)].count(b) != 0;
    }
    /// The neighbours of `a`, ascending.
    std::vector<NodeAddress> neighbors(NodeAddress a) const {
        if (!valid(a)) return {};
        const auto& s = adj_[static_cast<std::size_t>(a)];
        return {s.begin(), s.end()};
    }
    /// Cuts every link between `side` and the other nodes; returns the cut
    /// links (smaller address first, ascending), which connect() restores.
    std::vector<Link> partition(const std::vector<NodeAddress>& side) {
        std::set<NodeAddress> in;
        for (NodeAddress a : side) {
            if (valid(a)) in.insert(a);
        }
        std::set<Link> cut;
        for (NodeAddress a : in) {
            for (NodeAddress b : neighbors(a)) {
                if (in.count(b) == 0) cut.insert({a < b ? a : b, a < b ? b : a});
            }
        }
        for (const Link& l : cut)
            disconnect(l.first, l.second);
        return {cut.begin(), cut.end()};
    }

private:
    std::vector<std::set<NodeAddress>> adj_;
};

// --- nodes and the network --------------------------------------------------------

template <typename Msg> class Network;

/// What a protocol's adapter implements, once per protocol. Every callback
/// runs inside an event; the adapter answers through its NodePorts.
template <typename Msg> class INodeAdapter {
public:
    virtual ~INodeAdapter() = default;
    /// At time 0, in address order, once every node is installed.
    virtual void start() {}
    /// A message from one-hop neighbour `from`.
    virtual void receive(NodeAddress from, const Msg& msg) = 0;
    /// A unicast to `to` found no link (sent to a node that was not a
    /// neighbour at send time): the MAC's retry limit, with the message back.
    virtual void undelivered(NodeAddress /*to*/, const Msg& /*msg*/) {}
    /// A scripted link to `neighbor` came up or went down (Network::linkAt):
    /// what a link-state monitor reports. A protocol that learns neighbours
    /// only from its own messages ignores it.
    virtual void linkChanged(NodeAddress /*neighbor*/, bool /*up*/) {}
};

/// One node's ports and its transmit services, handed to the adapter.
/// It is the node's INeighborProvider and ITimerScheduler itself.
template <typename Msg> class NodePorts : public INeighborProvider, public ITimerScheduler {
public:
    NodePorts(Network<Msg>& net, NodeAddress self, std::uint64_t seed)
        : net_(net), self_(self), rng_(seed) {}

    NodeAddress address() const { return self_; }
    const IClock& clock() const { return net_.events(); }
    IRng& rng() { return rng_; }
    ITimerScheduler& timers() { return *this; }
    const INeighborProvider& neighborProvider() const { return *this; }

    /// INeighborProvider: the current neighbours, ascending.
    std::vector<NodeAddress> neighbors() const override { return net_.topology().neighbors(self_); }

    /// ITimerScheduler, on the bench's queue (cancellable).
    void schedule(Time delay, std::function<void()> callback) override {
        net_.events().schedule(delay, std::move(callback));
    }
    TimerHandle scheduleCancellable(Time delay, std::function<void()> callback) override {
        return net_.events().schedule(delay, std::move(callback));
    }
    void cancel(TimerHandle timer) override { net_.events().cancel(timer); }

    /// One-hop broadcast to every current neighbour.
    void broadcast(const Msg& msg) { net_.broadcast(self_, msg); }
    /// One-hop unicast; comes back through undelivered() without a link.
    void unicast(NodeAddress to, const Msg& msg) { net_.unicast(self_, to, msg); }

private:
    Network<Msg>& net_;
    NodeAddress self_;
    Rng rng_;
};

/// The bench's settings.
struct NetworkConfig {
    std::uint64_t seed = 1;
    Time macDelay = 0.001;  ///< send to receive, every hop
    std::size_t maxPendingEvents = EventQueue::kDefaultMaxPending;
    std::size_t maxTrace = std::size_t{1} << 16;  ///< records kept; the digest sees all
};

/// One thing that happened on the air, for the trace.
enum class TraceEvent : std::uint8_t {
    Broadcast = 0,    ///< from sent to every neighbour (to = kInvalidAddress)
    Unicast = 1,      ///< from sent to `to`
    Deliver = 2,      ///< `to` received from `from`
    Undelivered = 3,  ///< from's unicast to `to` found no link
    Lost = 4,         ///< from -> to dropped by setLoss
    LinkUp = 5,
    LinkDown = 6,
};

struct TraceRecord {
    Time t;
    TraceEvent event;
    NodeAddress from;
    NodeAddress to;
    std::uint8_t kind;  ///< the message's kind (KindOf), 0 without one
    bool operator==(const TraceRecord&) const = default;
};

/// N nodes, one adapter each, on a scripted topology with an ideal MAC.
template <typename Msg> class Network {
public:
    /// The kind byte of a message, for the counters and the trace.
    using KindOf = std::function<std::uint8_t(const Msg&)>;
    using Factory = std::function<std::unique_ptr<INodeAdapter<Msg>>(NodePorts<Msg>&)>;

    explicit Network(Topology topology, NetworkConfig config = {}, KindOf kindOf = {})
        : topology_(std::move(topology)), config_(config), kindOf_(std::move(kindOf)),
          events_(config.maxPendingEvents),
          channel_(Rng::streamSeed(config.seed, Rng::kChannelStream)),
          sentBy_(topology_.size(), 0) {
        for (std::size_t i = 0; i < topology_.size(); ++i) {
            ports_.push_back(std::make_unique<NodePorts<Msg>>(*this, static_cast<NodeAddress>(i),
                                                              Rng::streamSeed(config.seed, i)));
        }
    }
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    /// One adapter per node, made by `factory` in address order; each
    /// start() runs at time 0 as an event, in the same order.
    void install(const Factory& factory) {
        nodes_.clear();
        for (auto& p : ports_)
            nodes_.push_back(factory(*p));
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            events_.schedule(0.0, [this, i]() { nodes_[i]->start(); });
        }
    }

    std::size_t size() const { return ports_.size(); }
    INodeAdapter<Msg>& node(NodeAddress a) { return *nodes_.at(static_cast<std::size_t>(a)); }
    /// The adapter of `a` as its concrete type.
    template <typename T> T& nodeAs(NodeAddress a) { return static_cast<T&>(node(a)); }
    NodePorts<Msg>& ports(NodeAddress a) { return *ports_.at(static_cast<std::size_t>(a)); }
    Topology& topology() { return topology_; }
    const Topology& topology() const { return topology_; }
    EventQueue& events() { return events_; }
    Time now() const { return events_.now(); }

    // --- the script

    /// Runs `fn` at time `t`.
    void at(Time t, std::function<void()> fn) { events_.scheduleAt(t, std::move(fn)); }
    /// At time `t`, the link a - b comes up or goes down; both ends are told
    /// (linkChanged), lower address first. A no-op if it already is.
    void linkAt(Time t, NodeAddress a, NodeAddress b, bool up) {
        at(t, [this, a, b, up]() { setLink(a, b, up); });
    }
    /// The link a - b, now (as linkAt).
    void setLink(NodeAddress a, NodeAddress b, bool up) {
        if (!topology_.valid(a) || !topology_.valid(b) || a == b || topology_.linked(a, b) == up) {
            return;
        }
        if (up) {
            topology_.connect(a, b);
        } else {
            topology_.disconnect(a, b);
        }
        record({now(), up ? TraceEvent::LinkUp : TraceEvent::LinkDown, a < b ? a : b, a < b ? b : a,
                0});
        const NodeAddress lo = a < b ? a : b;
        const NodeAddress hi = a < b ? b : a;
        if (!nodes_.empty()) {
            node(lo).linkChanged(hi, up);
            node(hi).linkChanged(lo, up);
        }
    }
    /// From now on, a message from `from` to `to` is lost with probability
    /// `p` (0 clears it). Drawn from the channel's own stream, only on a
    /// lossy link, so a loss-free run draws nothing.
    void setLoss(NodeAddress from, NodeAddress to, double p) {
        if (p > 0.0) {
            loss_[{from, to}] = p;
        } else {
            loss_.erase({from, to});
        }
    }

    /// Runs the events due up to `until` (at most `maxEvents`); returns how
    /// many ran.
    std::uint64_t run(Time until,
                      std::uint64_t maxEvents = std::numeric_limits<std::uint64_t>::max()) {
        return events_.runUntil(until, maxEvents);
    }

    // --- the MAC (NodePorts calls these)

    void broadcast(NodeAddress from, const Msg& msg) {
        const std::uint8_t k = kindOf(msg);
        count(from, k);
        record({now(), TraceEvent::Broadcast, from, kInvalidAddress, k});
        for (NodeAddress to : topology_.neighbors(from))
            transmit(from, to, msg, k);
    }
    void unicast(NodeAddress from, NodeAddress to, const Msg& msg) {
        const std::uint8_t k = kindOf(msg);
        count(from, k);
        record({now(), TraceEvent::Unicast, from, to, k});
        if (!topology_.linked(from, to)) {
            ++undelivered_;
            events_.schedule(config_.macDelay, [this, from, to, msg, k]() {
                record({now(), TraceEvent::Undelivered, from, to, k});
                node(from).undelivered(to, msg);
            });
            return;
        }
        transmit(from, to, msg, k);
    }

    // --- what happened

    /// Messages sent (a broadcast counts once) of `kind`, by every node.
    std::uint64_t sent(std::uint8_t kind) const { return sentByKind_[kind]; }
    std::uint64_t sentBy(NodeAddress a) const { return sentBy_.at(static_cast<std::size_t>(a)); }
    std::uint64_t sentTotal() const { return sentTotal_; }
    std::uint64_t deliveredTotal() const { return delivered_; }
    std::uint64_t undeliveredTotal() const { return undelivered_; }
    std::uint64_t lostTotal() const { return lost_; }
    /// The first maxTrace records; traceDropped() counts the rest.
    const std::vector<TraceRecord>& trace() const { return trace_; }
    std::uint64_t traceDropped() const { return traceDropped_; }
    /// FNV-1a over every record, kept or not: equal digests, equal runs.
    std::uint64_t digest() const { return digest_; }

private:
    std::uint8_t kindOf(const Msg& msg) const { return kindOf_ ? kindOf_(msg) : 0; }

    void count(NodeAddress from, std::uint8_t k) {
        ++sentByKind_[k];
        ++sentBy_[static_cast<std::size_t>(from)];
        ++sentTotal_;
    }

    void transmit(NodeAddress from, NodeAddress to, const Msg& msg, std::uint8_t k) {
        auto it = loss_.find({from, to});
        if (it != loss_.end() && channel_.uniform() < it->second) {
            ++lost_;
            record({now(), TraceEvent::Lost, from, to, k});
            return;
        }
        events_.schedule(config_.macDelay, [this, from, to, msg, k]() {
            ++delivered_;
            record({now(), TraceEvent::Deliver, from, to, k});
            node(to).receive(from, msg);
        });
    }

    void record(const TraceRecord& r) {
        auto mix = [this](std::uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                digest_ ^= (v >> (8 * i)) & 0xFFu;
                digest_ *= 0x100000001B3ull;
            }
        };
        mix(std::bit_cast<std::uint64_t>(r.t));
        mix(static_cast<std::uint64_t>(r.event));
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(r.from)));
        mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(r.to)));
        mix(r.kind);
        if (trace_.size() < config_.maxTrace) {
            trace_.push_back(r);
        } else {
            ++traceDropped_;
        }
    }

    Topology topology_;
    NetworkConfig config_;
    KindOf kindOf_;
    EventQueue events_;
    Rng channel_;
    std::vector<std::unique_ptr<NodePorts<Msg>>> ports_;
    std::vector<std::unique_ptr<INodeAdapter<Msg>>> nodes_;
    std::map<std::pair<NodeAddress, NodeAddress>, double> loss_;
    std::array<std::uint64_t, 256> sentByKind_{};
    std::vector<std::uint64_t> sentBy_;
    std::uint64_t sentTotal_ = 0;
    std::uint64_t delivered_ = 0;
    std::uint64_t undelivered_ = 0;
    std::uint64_t lost_ = 0;
    std::vector<TraceRecord> trace_;
    std::uint64_t traceDropped_ = 0;
    std::uint64_t digest_ = 0xCBF29CE484222325ull;
};

}  // namespace swarm::core::testbench

#endif  // SWARM_CORE_TESTBENCH_H
