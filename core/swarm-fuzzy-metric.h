// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// FuzzyMetric: the fuzzy composite ILinkMetric (#3). It judges a path by
// three signals a protocol already has, through a small explicit rule table:
// its delay and its link stability set a quality in [0, 1], and its hop count
// divides it. New here, written fresh: no protocol of the org ships a fuzzy
// metric yet, and the issue names no single published rule set to follow, so
// every breakpoint and every rule below is a repo choice (#3), and any claim
// about its effect needs a paired A/B in a protocol that selects it.
//
// The inference, zero-order Takagi-Sugeno with product AND:
//
//   1. Each signal is fuzzified into three terms, Low, Medium, High, by
//      fuzzyMemberships(): shoulders below `low` and above `high`, a triangle
//      peaking at their midpoint. The three degrees always sum to 1.
//        - delay: the path's time per hop relative to the unloaded hop time,
//          obs.pathTime / (obs.hops * obs.hopTime); 1 is an unloaded path,
//          4 a path whose hops take four times as long.
//        - stability: obs.stability, 0..1 (LinkObservation).
//   2. Each of the nine rules "IF delay is D AND stability is S THEN quality
//      is rules[D][S]" fires with weight mu_D(delay) * mu_S(stability).
//   3. quality = sum of weight * rules[D][S]. The weights sum to 1, so this is
//      the bilinear interpolation of the rule table: continuous, in [0, 1]
//      when the table is, and monotone in a signal whenever the table is
//      monotone along it (the default table is: more delay never helps, more
//      stability never hurts).
//
// pheromone = quality / hops. Hops enter as plain hop count, so a path's
// length is never saturated by a membership, and an unknown signal reads as
// its best term ("no evidence against this link", the ILinkMetric contract):
// with neither signal known, quality is the best rule's and the metric ranks
// paths exactly as inverse hop count.
//
// Pure and stateless after construction (golden rule 10); no dependency
// beyond the standard library; every structure is a fixed-size array.
#ifndef SWARM_CORE_FUZZY_METRIC_H
#define SWARM_CORE_FUZZY_METRIC_H

#include "swarm-link-metric.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace swarm::core {

/// The three linguistic terms of every input, as rule-table indices.
enum class FuzzyTerm : std::size_t { Low = 0, Medium = 1, High = 2 };
inline constexpr std::size_t kFuzzyTerms = 3;

/// A crisp value's degree in each term. low + medium + high == 1.
struct FuzzyMemberships {
    double low = 0.0;
    double medium = 0.0;
    double high = 0.0;

    double operator[](FuzzyTerm t) const {
        return t == FuzzyTerm::Low ? low : (t == FuzzyTerm::Medium ? medium : high);
    }
};

/// Fuzzifies `x` against breakpoints `low` < `high`: fully Low at or below
/// `low`, fully High at or above `high`, fully Medium at their midpoint, and
/// linear between (each degree is a shoulder or a triangle). The caller
/// guarantees low < high, both finite, and x not NaN (+-inf are fine).
inline FuzzyMemberships fuzzyMemberships(double x, double low, double high) {
    FuzzyMemberships m;
    // Halves first: high - low can overflow for finite breakpoints of
    // opposite sign; low < x < high keeps every difference below finite.
    const double mid = low / 2.0 + high / 2.0;
    if (x <= low) {
        m.low = 1.0;
    } else if (x >= high) {
        m.high = 1.0;
    } else if (x < mid) {
        m.low = (mid - x) / (mid - low);
        m.medium = 1.0 - m.low;
    } else {
        m.high = (x - mid) / (high - mid);
        m.medium = 1.0 - m.high;
    }
    return m;
}

/// The tunables of FuzzyMetric, in one place (as AntHocNet's Config for its
/// metrics). Every default is a repo choice (#3): no source gives them.
struct FuzzyMetricParams {
    /// Delay breakpoints, as a ratio of the path's time per hop to the
    /// unloaded hop time: at or below 1.5 is Low, at or above 4 is High.
    double delayLow = 1.5;
    double delayHigh = 4.0;
    /// Stability breakpoints (0..1): at or below 0.25 is Low, at or above
    /// 0.75 is High.
    double stabilityLow = 0.25;
    double stabilityHigh = 0.75;
    /// rules[delay term][stability term]: the quality (0..1) the rule
    /// concludes. Rows are delay Low, Medium, High; columns stability Low,
    /// Medium, High. Non-increasing down every column, non-decreasing along
    /// every row, best (1) at low delay and high stability.
    std::array<std::array<double, kFuzzyTerms>, kFuzzyTerms> rules{{
        {0.40, 0.70, 1.00},  // delay Low
        {0.25, 0.50, 0.70},  // delay Medium
        {0.10, 0.25, 0.40},  // delay High
    }};
};

/// The fuzzy composite metric. Prices whole paths from LinkObservation's
/// aggregates, so it does not use link quality (hopCost stays 1).
class FuzzyMetric : public ILinkMetric {
public:
    /// The name a protocol's metric registry selects it by. Stable from API
    /// version 2 on.
    static constexpr std::string_view kName = "fuzzy";

    /// Takes `params` after validation: a breakpoint pair that is not finite
    /// or not strictly increasing reads as its default pair; a non-finite
    /// rule reads as its default, and every rule is clamped to [0, 1].
    explicit FuzzyMetric(const FuzzyMetricParams& params = {}) : params_(validated(params)) {}

    /// quality(obs) / obs.hops; 0 when hops is not > 0 (no path).
    double pheromone(const LinkObservation& obs) const override {
        return obs.hops > 0.0 ? quality(obs) / obs.hops : 0.0;
    }

    /// The rule table's conclusion for the path, in [0, 1]. Delay is unknown,
    /// and reads Low, when hops or hopTime is not > 0, pathTime is NaN or
    /// negative, or the ratio is undefined (inf / inf); stability is unknown, and reads High, when NaN. Other values
    /// outside 0..1 clamp to the shoulders.
    double quality(const LinkObservation& obs) const {
        const FuzzyMemberships delay = delayMemberships(obs);
        const FuzzyMemberships stability =
            std::isnan(obs.stability)
                ? FuzzyMemberships{0.0, 0.0, 1.0}
                : fuzzyMemberships(obs.stability, params_.stabilityLow, params_.stabilityHigh);
        double q = 0.0;
        for (std::size_t d = 0; d < kFuzzyTerms; ++d) {
            for (std::size_t s = 0; s < kFuzzyTerms; ++s) {
                q += delay[static_cast<FuzzyTerm>(d)] * stability[static_cast<FuzzyTerm>(s)] *
                     params_.rules[d][s];
            }
        }
        return q;
    }

    const FuzzyMetricParams& params() const { return params_; }

private:
    FuzzyMemberships delayMemberships(const LinkObservation& obs) const {
        if (!(obs.hops > 0.0) || !(obs.hopTime > 0.0) || !(obs.pathTime >= 0.0)) {
            return FuzzyMemberships{1.0, 0.0, 0.0};
        }
        const double ratio = obs.pathTime / (obs.hops * obs.hopTime);
        if (std::isnan(ratio)) return FuzzyMemberships{1.0, 0.0, 0.0};  // inf / inf
        return fuzzyMemberships(ratio, params_.delayLow, params_.delayHigh);
    }

    static bool validPair(double low, double high) {
        return std::isfinite(low) && std::isfinite(high) && low < high;
    }

    static FuzzyMetricParams validated(FuzzyMetricParams p) {
        const FuzzyMetricParams defaults;
        if (!validPair(p.delayLow, p.delayHigh)) {
            p.delayLow = defaults.delayLow;
            p.delayHigh = defaults.delayHigh;
        }
        if (!validPair(p.stabilityLow, p.stabilityHigh)) {
            p.stabilityLow = defaults.stabilityLow;
            p.stabilityHigh = defaults.stabilityHigh;
        }
        for (std::size_t d = 0; d < kFuzzyTerms; ++d) {
            for (std::size_t s = 0; s < kFuzzyTerms; ++s) {
                double& r = p.rules[d][s];
                if (!std::isfinite(r)) r = defaults.rules[d][s];
                r = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
            }
        }
        return p;
    }

    FuzzyMetricParams params_;
};

}  // namespace swarm::core

#endif  // SWARM_CORE_FUZZY_METRIC_H
