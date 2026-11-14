// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The fuzzy composite link metric (#3): the memberships at and around their
// breakpoints, every rule of the table, the interpolation between them,
// monotonicity, unknown signals, and parameter validation.
#include "../swarm-fuzzy-metric.h"
#include "swarm-core-tests.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace swarm::core::test {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-12;
}

bool is(const FuzzyMemberships& m, double low, double medium, double high) {
    return near(m.low, low) && near(m.medium, medium) && near(m.high, high);
}

/// A path of `hops` hops whose time per hop is `ratio` times the unloaded hop
/// time (0.01 s), with link stability `stability`.
LinkObservation path(double hops, double ratio, double stability) {
    LinkObservation o;
    o.hops = hops;
    o.hopTime = 0.01;
    o.pathTime = ratio * hops * o.hopTime;
    o.stability = stability;
    return o;
}

}  // namespace

void TestFuzzyMemberships(Checker& c) {
    // Breakpoints 1 and 3: Low up to 1, Medium peaks at 2, High from 3.
    c.check(is(fuzzyMemberships(-kInf, 1.0, 3.0), 1, 0, 0), "-inf: Low");
    c.check(is(fuzzyMemberships(0.0, 1.0, 3.0), 1, 0, 0), "below low: Low");
    c.check(is(fuzzyMemberships(1.0, 1.0, 3.0), 1, 0, 0), "at low: fully Low");
    c.check(is(fuzzyMemberships(1.5, 1.0, 3.0), 0.5, 0.5, 0), "low..mid: Low and Medium");
    c.check(is(fuzzyMemberships(2.0, 1.0, 3.0), 0, 1, 0), "at the midpoint: fully Medium");
    c.check(is(fuzzyMemberships(2.5, 1.0, 3.0), 0, 0.5, 0.5), "mid..high: Medium and High");
    c.check(is(fuzzyMemberships(3.0, 1.0, 3.0), 0, 0, 1), "at high: fully High");
    c.check(is(fuzzyMemberships(kInf, 1.0, 3.0), 0, 0, 1), "+inf: High");

    // Continuous at every breakpoint, and a partition of unity everywhere.
    const double eps = 1e-9;
    const FuzzyMemberships justAbove = fuzzyMemberships(1.0 + eps, 1.0, 3.0);
    const FuzzyMemberships justBelow = fuzzyMemberships(3.0 - eps, 1.0, 3.0);
    c.check(justAbove.low > 1.0 - 1e-8 && justAbove.medium < 1e-8, "continuous at low");
    c.check(justBelow.high > 1.0 - 1e-8 && justBelow.medium < 1e-8, "continuous at high");
    const FuzzyMemberships m1 = fuzzyMemberships(2.0 - eps, 1.0, 3.0);
    const FuzzyMemberships m2 = fuzzyMemberships(2.0 + eps, 1.0, 3.0);
    c.check(m1.medium > 1.0 - 1e-8 && m2.medium > 1.0 - 1e-8, "continuous at the midpoint");
    bool unity = true;
    bool inRange = true;
    for (int i = -10; i <= 50; ++i) {
        const FuzzyMemberships m = fuzzyMemberships(i * 0.1, 1.0, 3.0);
        unity = unity && near(m.low + m.medium + m.high, 1.0);
        inRange = inRange && m.low >= 0.0 && m.medium >= 0.0 && m.high >= 0.0 && m.low <= 1.0 &&
                  m.medium <= 1.0 && m.high <= 1.0;
    }
    c.check(unity && inRange, "degrees in [0, 1], summing to 1");
    // Breakpoints whose span overflows a double stay finite.
    const FuzzyMemberships wide = fuzzyMemberships(1.0, -1e308, 1e308);
    c.check(std::isfinite(wide.low) && std::isfinite(wide.medium) && std::isfinite(wide.high) &&
                near(wide.low + wide.medium + wide.high, 1.0),
            "a span past the double range: finite degrees summing to 1");
    FuzzyMetricParams widest;
    widest.stabilityLow = -1e308;
    widest.stabilityHigh = 1e308;
    LinkObservation o;
    o.hops = 2.0;
    o.stability = 1.0;
    const double q = FuzzyMetric(widest).quality(o);
    c.check(std::isfinite(q) && q >= 0.0 && q <= 1.0, "and the quality stays bounded");
    const FuzzyMemberships m = fuzzyMemberships(1.5, 1.0, 3.0);
    c.check(m[FuzzyTerm::Low] == m.low && m[FuzzyTerm::Medium] == m.medium &&
                m[FuzzyTerm::High] == m.high,
            "indexed by term");
}

void TestFuzzyMetric(Checker& c) {
    const FuzzyMetric metric;
    const ILinkMetric& seam = metric;
    const FuzzyMetricParams defaults;
    c.check(FuzzyMetric::kName == "fuzzy", "selected by the name \"fuzzy\"");
    c.check(!seam.usesLinkQuality() && seam.hopCost(HopSignals{}) == 1.0,
            "prices paths from aggregates, not hops");
    c.check(std::isnan(LinkObservation{}.stability), "stability defaults to unknown");
    const LinkObservation aggregate{3.0, 0.5, 0.01};  // the three original fields still init
    c.check(aggregate.hops == 3.0 && std::isnan(aggregate.stability), "the field is additive");

    // Every rule: a signal pair that sits on one term each fires that rule
    // alone, so the quality is exactly its conclusion (defaults: delay ratio
    // Low <= 1.5, Medium peak 2.75, High >= 4; stability Low <= 0.25, Medium
    // peak 0.5, High >= 0.75).
    const std::array<double, kFuzzyTerms> delayAt{1.0, 2.75, 5.0};
    const std::array<double, kFuzzyTerms> stabilityAt{0.1, 0.5, 0.9};
    bool corners = true;
    for (std::size_t d = 0; d < kFuzzyTerms; ++d) {
        for (std::size_t s = 0; s < kFuzzyTerms; ++s)
            corners = corners && near(metric.quality(path(4.0, delayAt[d], stabilityAt[s])),
                                      defaults.rules[d][s]);
    }
    c.check(corners, "each of the nine rules concludes alone at its corner");
    c.check(near(metric.quality(path(4.0, 1.5, 0.75)), 1.0) &&
                near(metric.quality(path(4.0, 4.0, 0.25)), 0.1),
            "the breakpoints themselves belong to the shoulders");

    // The table drives it: another table, the same corners.
    FuzzyMetricParams custom;
    double v = 0.05;
    for (auto& row : custom.rules) {
        for (double& r : row) {
            r = v;
            v += 0.1;
        }
    }
    const FuzzyMetric table(custom);
    bool driven = true;
    for (std::size_t d = 0; d < kFuzzyTerms; ++d) {
        for (std::size_t s = 0; s < kFuzzyTerms; ++s)
            driven = driven && near(table.quality(path(2.0, delayAt[d], stabilityAt[s])),
                                    table.params().rules[d][s]);
    }
    c.check(driven, "a custom rule table is what concludes");

    // Between rules: bilinear interpolation. Delay halfway Low..Medium (2.125)
    // at stability High: 0.5 * 1.0 + 0.5 * 0.7.
    c.check(near(metric.quality(path(4.0, 2.125, 0.9)), 0.85), "interpolates between two rules");
    // Delay halfway (2.125), stability halfway Low..Medium (0.375): the mean
    // of four rules.
    c.check(near(metric.quality(path(4.0, 2.125, 0.375)), (0.4 + 0.7 + 0.25 + 0.5) / 4.0),
            "interpolates between four rules");

    // Monotone, continuous and in [0, 1] over a grid that crosses every
    // breakpoint.
    bool delayMonotone = true;
    bool stabilityMonotone = true;
    bool bounded = true;
    for (int s = 0; s <= 20; ++s) {
        double previous = kInf;
        for (int d = 0; d <= 60; ++d) {
            const double q = metric.quality(path(3.0, 0.5 + d * 0.1, s * 0.05));
            delayMonotone = delayMonotone && q <= previous + 1e-15;
            bounded = bounded && q >= 0.0 && q <= 1.0;
            previous = q;
        }
    }
    for (int d = 0; d <= 60; ++d) {
        double previous = -kInf;
        for (int s = 0; s <= 20; ++s) {
            const double q = metric.quality(path(3.0, 0.5 + d * 0.1, s * 0.05));
            stabilityMonotone = stabilityMonotone && q >= previous - 1e-15;
            previous = q;
        }
    }
    c.check(delayMonotone, "more delay never helps");
    c.check(stabilityMonotone, "more stability never hurts");
    c.check(bounded, "the quality stays in [0, 1]");

    // The pheromone: quality over hops; hop count is never saturated.
    c.check(near(seam.pheromone(path(4.0, 1.0, 0.9)), 0.25), "best signals: inverse hop count");
    c.check(near(seam.pheromone(path(4.0, 5.0, 0.1)), 0.1 / 4.0), "worst signals: scaled down");
    c.check(seam.pheromone(path(12.0, 1.0, 0.9)) < seam.pheromone(path(10.0, 1.0, 0.9)),
            "12 hops still rank below 10");
    c.check(seam.pheromone(path(2.0, 1.0, 0.1)) < seam.pheromone(path(3.0, 1.0, 0.9)),
            "a short unstable path can rank below a longer stable one");
    LinkObservation none;
    c.check(seam.pheromone(none) == 0.0, "no hops: no pheromone");
    none.hops = kNaN;
    c.check(seam.pheromone(none) == 0.0, "NaN hops: no pheromone");
    none.hops = -1.0;
    c.check(seam.pheromone(none) == 0.0, "negative hops: no pheromone");

    // Unknown signals read as their best term.
    LinkObservation unknown;
    unknown.hops = 5.0;
    c.check(metric.quality(unknown) == 1.0 && near(seam.pheromone(unknown), 0.2),
            "nothing known: the best rule, ranking as inverse hop count");
    LinkObservation noHopTime = path(5.0, 5.0, 0.1);
    noHopTime.hopTime = 0.0;
    c.check(near(metric.quality(noHopTime), 0.4), "no hop time: delay reads Low");
    LinkObservation nanTime = path(5.0, 5.0, 0.1);
    nanTime.pathTime = kNaN;
    LinkObservation negTime = path(5.0, 5.0, 0.1);
    negTime.pathTime = -1.0;
    c.check(near(metric.quality(nanTime), 0.4) && near(metric.quality(negTime), 0.4),
            "a NaN or negative path time: delay reads Low");
    LinkObservation infinite = path(5.0, 1.0, 0.9);
    infinite.pathTime = kInf;
    c.check(near(metric.quality(infinite), 0.4), "an infinite path time: delay High");
    infinite.hops = kInf;
    c.check(seam.pheromone(infinite) == 0.0, "infinite hops: no pheromone, no NaN");
    c.check(near(metric.quality(path(5.0, 5.0, kNaN)), 0.4), "unknown stability reads High");
    c.check(near(metric.quality(path(5.0, 1.0, -0.5)), 0.4) &&
                near(metric.quality(path(5.0, 1.0, 1.5)), 1.0),
            "stability outside 0..1 clamps to the shoulders");

    // Validation: bad breakpoints fall back by pair, bad rules by entry.
    FuzzyMetricParams bad;
    bad.delayLow = 5.0;
    bad.delayHigh = 2.0;
    bad.stabilityLow = kNaN;
    bad.rules[0][0] = kNaN;
    bad.rules[1][1] = 2.0;
    bad.rules[2][2] = -1.0;
    const FuzzyMetric fixed(bad);
    const FuzzyMetricParams& p = fixed.params();
    c.check(p.delayLow == defaults.delayLow && p.delayHigh == defaults.delayHigh,
            "inverted delay breakpoints fall back to the default pair");
    c.check(p.stabilityLow == defaults.stabilityLow && p.stabilityHigh == defaults.stabilityHigh,
            "a NaN stability breakpoint falls back to the default pair");
    c.check(p.rules[0][0] == defaults.rules[0][0] && p.rules[1][1] == 1.0 && p.rules[2][2] == 0.0,
            "a NaN rule reads as its default; rules clamp to [0, 1]");
    FuzzyMetricParams equal;
    equal.delayLow = equal.delayHigh = 2.0;
    c.check(FuzzyMetric(equal).params().delayLow == defaults.delayLow,
            "equal breakpoints fall back too");

    // Pure: the same observation, the same bits, any number of times.
    const LinkObservation o = path(3.0, 2.3, 0.6);
    const double first = seam.pheromone(o);
    bool same = true;
    for (int i = 0; i < 100; ++i)
        same = same && seam.pheromone(o) == first;
    c.check(same, "deterministic");
}

}  // namespace swarm::core::test
