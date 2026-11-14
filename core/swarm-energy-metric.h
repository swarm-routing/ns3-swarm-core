// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// EnergyMetric: the energy-aware ILinkMetric (#2), the line of BeeAdHoc and
// EEABR (Camilo et al., 2006, https://doi.org/10.1007/11839088_5): paths
// through energy-poor nodes cost more, so traffic shifts off drained nodes
// before they die and the network lives longer.
//
// Derived from swarm-routing/AntHocNet
// core/include/anthocnet/core/link_metric.h @ 78c4640 (MIT OR GPL-2.0-only),
// its EnergyMetric (AntHocNet #145, shipped selectable and default-off in its
// v2.3.0). The arithmetic is unchanged, so a protocol that moves from
// AntHocNet's metric to this one gets the same values bit for bit (#14); the
// only difference is that the floor is a constructor argument here instead of
// a HopSignals field read from AntHocNet's Config.
//
// How a path is priced. The metric usesLinkQuality(): each node prices its
// own hop, from its own battery (the IEnergy port, read into
// HopSignals::residualEnergy), as hopCost() = 1 / max(e, floor), and writes
// that cost into the hop an agent came over. A path costs the sum over its
// relays, carried by the protocol in LinkObservation::pathTime, and the
// pheromone is the inverse of max(cost, hops): every hop costs at least 1.
// Each node reads only its own energy, so nothing new travels on any wire:
// one scalar per hop, which the protocol's agents already carry (the wire
// decision of #2, taken in AntHocNet #145: no per-hop energy vector, no
// minimum-energy field, no wire-version bump).
//
// Unknown energy (NaN: no energy model) costs 1, a plain hop, so the metric
// degrades to hop count, never to a wrong ranking (the IEnergy contract).
#ifndef SWARM_CORE_ENERGY_METRIC_H
#define SWARM_CORE_ENERGY_METRIC_H

#include "swarm-link-metric.h"

#include <cmath>
#include <string_view>

namespace swarm::core {

/// The cost of one hop through a node with residual energy fraction
/// `residual` (0..1): 1 / max(residual, floor), 1 for a full (>= 1) or
/// unknown (NaN) battery. `floor` (in (0, 1]) keeps an empty node's cost large
/// but finite: at the default 0.01 a drained relay costs 100 hops.
inline double energyHopCost(double residual, double floor) {
    if (std::isnan(residual)) return 1.0;
    const double e = residual < floor ? floor : residual;
    return e >= 1.0 ? 1.0 : 1.0 / e;
}

/// The energy-aware metric. Stateless after construction and pure: it reads
/// only what it is handed (golden rule 10), so it is safe to share between
/// nodes and threads.
class EnergyMetric : public ILinkMetric {
public:
    /// The name a protocol's metric registry selects it by ("energy", as in
    /// AntHocNet's registry). Stable from API version 2 on.
    static constexpr std::string_view kName = "energy";
    /// The default floor: AntHocNet's Config::energyCostFloor (a repo choice
    /// there, AntHocNet docs/configuration.md; no paper gives it).
    static constexpr double kDefaultFloor = 0.01;

    /// `floor` is the residual fraction below which a node costs no more
    /// (see energyHopCost). A non-finite or non-positive floor reads as
    /// kDefaultFloor; one above 1 reads as 1 (every hop then costs 1).
    explicit EnergyMetric(double floor = kDefaultFloor)
        : floor_(!std::isfinite(floor) || floor <= 0.0 ? kDefaultFloor
                                                       : (floor > 1.0 ? 1.0 : floor)) {}

    /// 1 / max(obs.pathTime, obs.hops), 0 when both are <= 0: pathTime holds
    /// the sum of the hops' costs; a NaN pathTime counts as plain hops.
    double pheromone(const LinkObservation& obs) const override {
        const double cost = obs.pathTime > obs.hops ? obs.pathTime : obs.hops;
        return cost > 0.0 ? 1.0 / cost : 0.0;
    }

    bool usesLinkQuality() const override { return true; }

    /// energyHopCost(signals.residualEnergy, floor()). The SINR is not read.
    double hopCost(const HopSignals& signals) const override {
        return energyHopCost(signals.residualEnergy, floor_);
    }

    double floor() const { return floor_; }

private:
    double floor_;
};

}  // namespace swarm::core

#endif  // SWARM_CORE_ENERGY_METRIC_H
