// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// ILinkMetric: the strategy that turns a path observation into a pheromone
// value. Derived from swarm-routing/AntHocNet
// core/include/anthocnet/core/link_metric.h @ 78c4640 (MIT OR GPL-2.0-only).
// The seam is shared; the concrete metrics stay with their protocols or arrive
// as their own issues: the energy-aware EnergyMetric (#2,
// swarm-energy-metric.h), the fuzzy composite FuzzyMetric (#3,
// swarm-fuzzy-metric.h). AntHocNet's ClassicMetric (its Eq. 2) and SinrMetric
// stay behind (golden rule 11).
//
// A metric is pure: it reads only what it is handed, never a simulator, clock
// or RNG. Adapter-supplied signals (SINR, residual energy) arrive in
// HopSignals, read from the ILinkQuality and IEnergy ports.
#ifndef SWARM_CORE_LINK_METRIC_H
#define SWARM_CORE_LINK_METRIC_H

#include <limits>

namespace swarm::core {

/// What an agent knows about the path from this node to the destination:
/// aggregates only, never per-hop downstream vectors.
struct LinkObservation {
    double hops = 0.0;      ///< hops to the destination (may be smoothed).
    double pathTime = 0.0;  ///< accumulated time or cost estimate.
    double hopTime = 0.0;   ///< the protocol's unloaded per-hop time (seconds).
    /// The path's link stability, 0..1: 1 for links that have stayed up, 0
    /// for links that keep breaking. The protocol measures it (for example
    /// the minimum over the path's links of a neighbour's uptime ratio, from
    /// its own neighbour churn history) and carries it as one aggregate; NaN
    /// when it does not, which every metric reads as "no evidence against
    /// this path". Read by the fuzzy composite metric (#3).
    double stability = std::numeric_limits<double>::quiet_NaN();
};

/// What a node knows about one of its own links. Unknown signals are NaN, and
/// every metric reads NaN as "no evidence against this link".
struct HopSignals {
    double sinrDb = std::numeric_limits<double>::quiet_NaN();          ///< ILinkQuality
    double residualEnergy = std::numeric_limits<double>::quiet_NaN();  ///< IEnergy, 0..1
};

/// Maps an observation to a pheromone (goodness; higher is better).
class ILinkMetric {
public:
    virtual ~ILinkMetric() = default;
    virtual double pheromone(const LinkObservation& obs) const = 0;
    /// True when the metric prices links (hopCost) rather than time: each node
    /// then writes its own link's cost into the hop an agent came over.
    virtual bool usesLinkQuality() const { return false; }
    /// The cost of one of this node's links, for a metric that
    /// usesLinkQuality(). 1 (a plain hop) by default.
    virtual double hopCost(const HopSignals& /*signals*/) const { return 1.0; }
};

}  // namespace swarm::core

#endif  // SWARM_CORE_LINK_METRIC_H
