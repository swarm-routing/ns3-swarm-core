// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The router observer (#42): a fake router drives every callback, the counters
// count them, bounded, and attaching an observer changes no decision. The
// shape follows swarm-routing/AntHocNet core/tests/test_observability.cpp
// @ 78c4640 (MIT OR GPL-2.0-only).
#include "../swarm-observer.h"
#include "swarm-core-tests.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace swarm::core::test {

namespace {

struct FakeClock : IClock {
    Time t = 0.0;
    Time now() const override { return t; }
};

/// Records every event, in order.
struct RecordingObserver : IRouterObserver {
    std::vector<int> events;
    void onControlSent(std::uint8_t kind, bool broadcast, std::uint32_t bytes) override {
        events.insert(events.end(), {1, kind, broadcast ? 1 : 0, static_cast<int>(bytes)});
    }
    void onControlReceived(std::uint8_t kind, std::uint32_t bytes) override {
        events.insert(events.end(), {2, kind, static_cast<int>(bytes)});
    }
    void onRouteChanged(NodeAddress dest, NodeAddress nextHop, bool added) override {
        events.insert(events.end(), {3, dest, nextHop, added ? 1 : 0});
    }
    void onDataDecision(NodeAddress dest, RouteAction action) override {
        events.insert(events.end(), {4, dest, static_cast<int>(action)});
    }
    void onStateSize(std::uint32_t entries, std::uint32_t bytes) override {
        events.insert(events.end(), {5, static_cast<int>(entries), static_cast<int>(bytes)});
    }
};

constexpr std::uint8_t kQuery = 1;
constexpr std::uint8_t kReply = 2;
constexpr std::array<KindName, 2> kKinds{{{kQuery, "QUERY"}, {kReply, "REPLY"}}};

/// A fake router with no protocol in it: queries for unknown destinations,
/// learns a route from each reply, forwards data on a known route. It reports
/// through the optional observer, as a protocol's core does.
class FakeRouter {
public:
    explicit FakeRouter(IRouterObserver* observer) : observer_(observer) {}

    RouteDecision<std::uint8_t> data(NodeAddress dest) {
        if (dest == self_) {
            report(dest, RouteAction::Deliver);
            return RouteDecision<std::uint8_t>::deliver();
        }
        if (dest < 0) {
            report(dest, RouteAction::Drop);
            return RouteDecision<std::uint8_t>::drop();
        }
        auto it = routes_.find(dest);
        if (it == routes_.end()) {
            report(dest, RouteAction::Queue);
            if (observer_) observer_->onControlSent(kQuery, true, 12);
            return RouteDecision<std::uint8_t>::queue(dest);
        }
        report(dest, RouteAction::Unicast);
        return RouteDecision<std::uint8_t>::unicast(it->second, 0);
    }

    void reply(NodeAddress dest, NodeAddress via) {
        if (observer_) observer_->onControlReceived(kReply, 20);
        const bool added = routes_.emplace(dest, via).second;
        if (observer_ && added) {
            observer_->onRouteChanged(dest, via, true);
            observer_->onStateSize(static_cast<std::uint32_t>(routes_.size()),
                                   static_cast<std::uint32_t>(routes_.size() * 8));
        }
    }

    void lose(NodeAddress dest) {
        auto it = routes_.find(dest);
        if (it == routes_.end()) return;
        const NodeAddress via = it->second;
        routes_.erase(it);
        if (observer_) {
            observer_->onRouteChanged(dest, via, false);
            observer_->onStateSize(static_cast<std::uint32_t>(routes_.size()),
                                   static_cast<std::uint32_t>(routes_.size() * 8));
        }
    }

private:
    void report(NodeAddress dest, RouteAction a) {
        if (observer_) observer_->onDataDecision(dest, a);
    }

    IRouterObserver* observer_;
    NodeAddress self_ = 0;
    std::map<NodeAddress, NodeAddress> routes_;
};

/// One scripted run; returns every decision, as (action, next hop) pairs.
std::vector<int> Script(FakeRouter& r, FakeClock& clock) {
    std::vector<int> out;
    auto record = [&out](const RouteDecision<std::uint8_t>& d) {
        out.push_back(static_cast<int>(d.action));
        out.push_back(d.nextHop);
    };
    clock.t = 0.0;
    record(r.data(5));  // queue + query
    clock.t = 1.0;
    r.reply(5, 2);  // 1 entry from t=1
    record(r.data(5));
    record(r.data(0));   // deliver
    record(r.data(-1));  // drop
    clock.t = 3.0;
    r.reply(6, 3);  // 2 entries from t=3
    clock.t = 4.0;
    r.lose(5);  // 1 entry from t=4
    r.lose(5);  // nothing to lose: no event
    record(r.data(6));
    clock.t = 6.0;
    return out;
}

}  // namespace

void TestObserverCallbacks(Checker& c) {
    // The base interface is all no-ops: a protocol overrides only what it reads.
    IRouterObserver silent;
    silent.onControlSent(1, true, 1);
    silent.onControlReceived(1, 1);
    silent.onRouteChanged(1, 2, true);
    silent.onDataDecision(1, RouteAction::Drop);
    silent.onStateSize(1, 1);

    FakeClock clock;
    RecordingObserver rec;
    FakeRouter r(&rec);
    Script(r, clock);
    constexpr int kQueue = static_cast<int>(RouteAction::Queue);
    constexpr int kUnicast = static_cast<int>(RouteAction::Unicast);
    constexpr int kDeliver = static_cast<int>(RouteAction::Deliver);
    constexpr int kDrop = static_cast<int>(RouteAction::Drop);
    // clang-format off
    const std::vector<int> expected{
        4, 5, kQueue,  1, kQuery, 1, 12,                // queue + query
        2, kReply, 20, 3, 5, 2, 1, 5, 1, 8,             // reply: route, state
        4, 5, kUnicast,                                 // forward
        4, 0, kDeliver,                                 // deliver
        4, -1, kDrop,                                   // drop
        2, kReply, 20, 3, 6, 3, 1, 5, 2, 16,            // second route
        3, 5, 2, 0, 5, 1, 8,                            // lost
        4, 6, kUnicast,                                 // forward
    };
    // clang-format on
    c.check(rec.events == expected, "every callback fires once per event, in order");

    c.check(findKindName(kKinds, kQuery) && std::strcmp(findKindName(kKinds, kQuery), "QUERY") == 0,
            "kind name");
    c.check(findKindName(kKinds, kReply) && std::strcmp(findKindName(kKinds, kReply), "REPLY") == 0,
            "second kind name");
    c.check(findKindName(kKinds, 9) == nullptr, "unnamed kind");
    c.check(findKindName({}, kQuery) == nullptr, "empty table");
}

void TestObserverCounters(Checker& c) {
    FakeClock clock;
    CountingObserver counting(clock);
    FakeRouter r(&counting);
    Script(r, clock);
    const RouterStats s = counting.stats();
    c.check(s.control.size() == 2, "one counter per kind seen");
    c.check(s.control.at(kQuery) == ControlCounters{1, 12, 1, 0, 0}, "query sent, broadcast");
    c.check(s.control.at(kReply) == ControlCounters{0, 0, 0, 2, 40}, "replies received");
    c.check(s.routeAdds == 2 && s.routeRemoves == 1, "route adds and removes");
    c.check(s.decisions(RouteAction::Queue) == 1 && s.decisions(RouteAction::Unicast) == 2 &&
                s.decisions(RouteAction::Deliver) == 1 && s.decisions(RouteAction::Drop) == 1,
            "data decisions by action");
    c.check(s.decisions(RouteAction::Broadcast) == 0, "no broadcast decision");
    c.check(s.stateEntries == 1 && s.stateBytes == 8, "last footprint");
    c.check(s.peakStateEntries == 2 && s.peakStateBytes == 16, "peak footprint");
    // Entries: 0 on [0,1), 1 on [1,3), 2 on [3,4), 1 on [4,6): area 6 over 6 s.
    c.check(s.observedFor == 6.0, "window from construction to now");
    c.check(std::fabs(s.meanStateEntries - 1.0) < 1e-12, "time-averaged entries");
    c.check(std::fabs(s.meanStateBytes - 8.0) < 1e-12, "time-averaged bytes");
    c.check(counting.stats() == s, "reading the counters changes nothing");

    // Bounded: every possible kind gives 256 counters, never more.
    for (int round = 0; round < 3; ++round) {
        for (int k = 0; k < 256; ++k) {
            counting.onControlSent(static_cast<std::uint8_t>(k), false, 1);
        }
    }
    c.check(counting.stats().control.size() == 256, "at most 256 kinds");
    c.check(counting.stats().control.at(7).txPackets == 3, "repeats count, not grow");

    // Aggregation sums nodes.
    RouterStats net = s;
    net.merge(s);
    c.check(net.control.at(kReply).rxBytes == 80 && net.routeAdds == 4, "merge sums counts");
    c.check(net.peakStateEntries == 4 && std::fabs(net.meanStateBytes - 16.0) < 1e-12,
            "merge sums footprints");
    c.check(net.observedFor == 6.0, "merge keeps the window");

    counting.reset();
    clock.t = 8.0;
    const RouterStats z = counting.stats();
    c.check(z.control.empty() && z.routeAdds == 0 && z.peakStateEntries == 0,
            "reset zeroes the counters");
    c.check(z.observedFor == 2.0 && z.meanStateEntries == 0.0, "reset restarts the window");
}

void TestObserverNonIntrusive(Checker& c) {
    // Same script with no observer, a recording one and a counting one: the
    // router's decisions are identical (an observer only reads, org rule 5).
    FakeClock clock;
    FakeRouter bare(nullptr);
    const std::vector<int> without = Script(bare, clock);
    RecordingObserver rec;
    FakeRouter recorded(&rec);
    const std::vector<int> withRecorder = Script(recorded, clock);
    CountingObserver counting(clock);
    FakeRouter counted(&counting);
    const std::vector<int> withCounter = Script(counted, clock);
    c.check(!without.empty(), "the script decides");
    c.check(withRecorder == without, "recording observer: same decisions");
    c.check(withCounter == without, "counting observer: same decisions");
}

}  // namespace swarm::core::test
