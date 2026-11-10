// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Roulette selection draws only through the injected RNG, one draw per
// selection, and a fixed seed gives a byte-identical selection sequence (org
// rule 5); plus RouteDecision, the ports and the link-metric seam.
#include "../swarm-link-metric.h"
#include "../swarm-ports.h"
#include "../swarm-route-decision.h"
#include "../swarm-selection.h"
#include "swarm-core-tests.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace swarm::core::test {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

std::vector<NodeAddress> sequence(std::uint64_t seed, int n) {
    const std::array<WeightedHop, 4> c{{{1, 1.0}, {2, 2.0}, {3, 0.5}, {4, 4.5}}};
    SeededRng rng(seed);
    std::vector<NodeAddress> out;
    for (int i = 0; i < n; ++i)
        out.push_back(rouletteSelect(c, rng));
    return out;
}

}  // namespace

void TestRoulette(Checker& c) {
    SeededRng rng(1);
    c.check(rouletteSelect({}, rng) == kInvalidAddress && rng.draws() == 0,
            "no candidates: no route, no draw");
    const std::array<WeightedHop, 3> none{{{1, 0.0}, {2, -1.0}, {3, std::nan("")}}};
    c.check(rouletteSelect(none, rng) == kInvalidAddress && rng.draws() == 0,
            "no usable weight: no route, no draw");

    const std::array<WeightedHop, 5> mixed{{{1, 0.0}, {2, 1.0}, {3, kInf}, {4, 3.0}, {5, -2.0}}};
    FixedRng low(0.0);
    c.check(rouletteSelect(mixed, low) == 2, "u = 0 picks the first usable candidate");
    FixedRng high(std::nextafter(1.0, 0.0));
    c.check(rouletteSelect(mixed, high) == 4, "u -> 1 picks the last usable candidate");
    FixedRng mid(0.25);  // 0.25 * 4 = 1.0: exactly the first candidate's share
    c.check(rouletteSelect(mixed, mid) == 4, "a boundary goes to the next candidate");

    // Frequencies follow the weights.
    const std::array<WeightedHop, 3> three{{{10, 1.0}, {20, 2.0}, {30, 7.0}}};
    SeededRng f(42);
    std::array<int, 3> count{};
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        const NodeAddress pick = rouletteSelect(three, f);
        count[static_cast<std::size_t>(pick / 10 - 1)] += 1;
    }
    c.check(f.draws() == n, "exactly one draw per selection");
    c.check(std::fabs(static_cast<double>(count[0]) / n - 0.1) < 0.01 &&
                std::fabs(static_cast<double>(count[1]) / n - 0.2) < 0.01 &&
                std::fabs(static_cast<double>(count[2]) / n - 0.7) < 0.01,
            "frequencies follow the weights");

    // From the table: (value / best)^beta, the previous hop excluded unless
    // it is the only way.
    PheromoneTable t(16);
    t.set(5, 1, 0.2);
    t.set(5, 2, 0.4);
    t.set(5, 3, 0.0);
    const auto w = selectionWeights(t, 5, 2.0);
    c.check(w.size() == 2 && w[0].node == 1 && std::fabs(w[0].weight - 0.25) < 1e-15 &&
                w[1].node == 2 && w[1].weight == 1.0,
            "weights are (value / best)^beta, zero values left out");
    const auto d = selectionDistribution(t, 5, 2.0);
    c.check(d.size() == 2 && std::fabs(d[0].weight - 0.2) < 1e-15 &&
                std::fabs(d[0].weight + d[1].weight - 1.0) < 1e-15,
            "distribution sums to 1");
    c.check(selectionWeights(t, 5, 1.0, 2).size() == 1 &&
                selectionWeights(t, 5, 1.0, 2)[0].node == 1,
            "the excluded hop is left out");
    t.set(6, 2, 1.0);
    c.check(selectionWeights(t, 6, 1.0, 2).size() == 1, "unless it is the only way");
    SeededRng s(3);
    c.check(selectNextHop(t, 99, 1.0, s) == kInvalidAddress && s.draws() == 0,
            "unknown destination: no route, no draw");
    c.check(selectNextHop(t, 6, 1.0, s, 2) == 2 && s.draws() == 1, "only way taken");
    c.check(selectionDistribution(t, 99, 1.0).empty(), "no distribution without a route");

    // A large beta cannot overflow: the weights are relative to the best.
    PheromoneTable big(4);
    big.set(1, 1, 1e10);
    big.set(1, 2, 2e10);
    const auto bw = selectionWeights(big, 1, 50.0);
    c.check(bw.size() == 2 && std::isfinite(bw[0].weight) && bw[1].weight == 1.0,
            "large beta stays finite");
    SeededRng b(9);
    c.check(selectNextHop(big, 1, 50.0, b) == 2, "and still selects");
}

void TestSelectionDeterminism(Checker& c) {
    const auto a = sequence(2026, 1000);
    c.check(a == sequence(2026, 1000), "same seed, byte-identical selection sequence");
    c.check(a != sequence(2027, 1000), "another seed, another sequence");
    // Pinned: SplitMix64 and the roulette are plain IEEE arithmetic, so this
    // sequence is the same on every compiler, platform and profile.
    const std::vector<NodeAddress> pinned{4, 4, 4, 3, 4, 4, 4, 4, 2, 2, 2, 4, 2, 2, 4, 2};
    c.check(std::vector<NodeAddress>(a.begin(), a.begin() + 16) == pinned,
            "the pinned sequence for seed 2026");
}

void TestRouteDecision(Checker& c) {
    struct Msg {
        int id = 0;
    };
    using D = RouteDecision<Msg>;
    c.check(D::none().action == RouteAction::None && !D::none().message, "none");
    c.check(D::drop().action == RouteAction::Drop && D::drop().nextHop == kInvalidAddress, "drop");
    c.check(D::deliver().action == RouteAction::Deliver, "deliver");
    c.check(D::queue(7).action == RouteAction::Queue && D::queue(7).nextHop == 7, "queue");
    c.check(D::discardPending(8).action == RouteAction::DiscardPending &&
                D::discardPending(8).nextHop == 8,
            "discardPending");
    const D u = D::unicast(3, Msg{42});
    c.check(u.action == RouteAction::Unicast && u.nextHop == 3 && u.message && u.message->id == 42,
            "unicast carries its message");
    const D b = D::broadcast(Msg{5});
    c.check(b.action == RouteAction::Broadcast && b.nextHop == kInvalidAddress && b.message &&
                b.message->id == 5,
            "broadcast carries its message");
}

void TestPortsAndMetric(Checker& c) {
    // A protocol sees time, neighbours, timers and link signals only through
    // the ports; these fakes are what an adapter or a unit test injects.
    struct Clock : IClock {
        Time t = 1.5;
        Time now() const override { return t; }
    } clock;
    struct Neighbors : INeighborProvider {
        std::vector<NodeAddress> neighbors() const override { return {2, 3}; }
    } nbs;
    struct Timers : ITimerScheduler {
        std::vector<Time> delays;
        void schedule(Time delay, std::function<void()> cb) override {
            delays.push_back(delay);
            cb();
        }
    } timers;
    struct Link : ILinkState, ILinkQuality, IEnergy {
        int macQueueLength(NodeAddress nh) const override { return nh == 2 ? 4 : 0; }
        Time macServiceTime(NodeAddress /*nh*/) const override { return 0.002; }
        double sinrDbFrom(NodeAddress /*nb*/) const override { return std::nan(""); }
        double residualEnergyFraction() const override { return 0.5; }
    } link;
    const IClock& ic = clock;
    const INeighborProvider& in = nbs;
    ITimerScheduler& it = timers;
    int fired = 0;
    it.schedule(0.25, [&fired] { ++fired; });
    c.check(ic.now() == 1.5 && in.neighbors().size() == 2, "clock and neighbours");
    c.check(fired == 1 && timers.delays == std::vector<Time>{0.25}, "timer");
    const ILinkState& ls = link;
    const ILinkQuality& lq = link;
    const IEnergy& en = link;
    c.check(ls.macQueueLength(2) == 4 && ls.macServiceTime(kInvalidAddress) == 0.002, "link state");
    c.check(std::isnan(lq.sinrDbFrom(2)) && en.residualEnergyFraction() == 0.5,
            "link quality and energy");

    // The metric seam: defaults, and a protocol's own metric through it.
    struct InverseHops : ILinkMetric {
        double pheromone(const LinkObservation& o) const override {
            return o.hops > 0.0 ? 1.0 / o.hops : 0.0;
        }
    } metric;
    const ILinkMetric& m = metric;
    LinkObservation obs;
    obs.hops = 4.0;
    c.check(m.pheromone(obs) == 0.25, "a metric through the seam");
    const HopSignals sig;
    c.check(!m.usesLinkQuality() && m.hopCost(sig) == 1.0, "seam defaults: a plain hop");
    c.check(std::isnan(sig.sinrDb) && std::isnan(sig.residualEnergy),
            "unknown signals are NaN, not 0");
}

}  // namespace swarm::core::test
