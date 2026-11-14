// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The concrete link metrics behind the ILinkMetric seam: the energy-aware
// metric (#2).
#include "../swarm-energy-metric.h"
#include "swarm-core-tests.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace swarm::core::test {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

/// AntHocNet's EnergyMetric::hopCost @ 78c4640, verbatim but for the floor
/// argument: the reference this metric must reproduce bit for bit (#14).
double anthocnetHopCost(double residual, double floor) {
    if (std::isnan(residual)) return 1.0;
    const double e = residual < floor ? floor : residual;
    return e >= 1.0 ? 1.0 : 1.0 / e;
}

/// AntHocNet's EnergyMetric::pheromone @ 78c4640, verbatim.
double anthocnetPheromone(double hops, double pathTime) {
    const double cost = pathTime > hops ? pathTime : hops;
    return cost > 0.0 ? 1.0 / cost : 0.0;
}

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

HopSignals energy(double e) {
    HopSignals s;
    s.residualEnergy = e;
    return s;
}

/// The cost of a path whose relays have the given residual fractions, as a
/// protocol sums it hop by hop.
template <std::size_t N>
double pathCost(const EnergyMetric& m, const std::array<double, N>& relays) {
    double cost = 0.0;
    for (const double e : relays)
        cost += m.hopCost(energy(e));
    return cost;
}

}  // namespace

void TestEnergyMetric(Checker& c) {
    const EnergyMetric m;
    const ILinkMetric& seam = m;
    c.check(EnergyMetric::kName == "energy", "selected by the name \"energy\"");
    c.check(seam.usesLinkQuality(), "each node prices its own hop");
    c.check(m.floor() == 0.01 && EnergyMetric::kDefaultFloor == 0.01, "default floor 0.01");

    // The per-hop cost: 1 / residual, floored, 1 for a full or unknown battery.
    c.check(seam.hopCost(energy(1.0)) == 1.0, "a full battery costs one hop");
    c.check(seam.hopCost(energy(0.5)) == 2.0, "half a battery costs two");
    c.check(seam.hopCost(energy(0.25)) == 4.0, "a quarter costs four");
    c.check(seam.hopCost(energy(0.01)) == 100.0, "at the floor: 100");
    c.check(seam.hopCost(energy(0.0)) == 100.0, "empty: the floor, finite");
    c.check(seam.hopCost(energy(-0.5)) == 100.0 && seam.hopCost(energy(-kInf)) == 100.0,
            "below zero reads as the floor");
    c.check(seam.hopCost(energy(1.5)) == 1.0 && seam.hopCost(energy(kInf)) == 1.0,
            "above full reads as full");
    c.check(seam.hopCost(energy(kNaN)) == 1.0 && seam.hopCost(HopSignals{}) == 1.0,
            "unknown energy is a plain hop, not a penalty");
    HopSignals withSinr = energy(0.5);
    withSinr.sinrDb = -10.0;
    c.check(seam.hopCost(withSinr) == 2.0, "the SINR is not read");

    // The floor is a parameter; invalid ones fall back.
    c.check(EnergyMetric(0.1).hopCost(energy(0.0)) == 10.0, "a floor of 0.1 caps the cost at 10");
    c.check(EnergyMetric(0.0).floor() == 0.01 && EnergyMetric(-1.0).floor() == 0.01 &&
                EnergyMetric(kNaN).floor() == 0.01 && EnergyMetric(kInf).floor() == 0.01,
            "a non-finite or non-positive floor reads as the default");
    const EnergyMetric tiny(1e-320);  // subnormal: its reciprocal overflows
    c.check(tiny.floor() == std::numeric_limits<double>::min() &&
                std::isfinite(tiny.hopCost(energy(0.0))),
            "a subnormal floor reads as the smallest normal: an empty node stays finite");
    LinkObservation viaEmpty;
    viaEmpty.hops = 1.0;
    viaEmpty.pathTime = tiny.hopCost(energy(0.0));
    c.check(tiny.pheromone(viaEmpty) > 0.0, "and its path keeps a positive pheromone");
    c.check(EnergyMetric(2.0).floor() == 1.0 && EnergyMetric(2.0).hopCost(energy(0.0)) == 1.0,
            "a floor above 1 reads as 1: every hop costs 1");

    // The pheromone: the inverse of the summed cost, never below the hops.
    LinkObservation o;
    o.hops = 3.0;
    c.check(seam.pheromone(o) == 1.0 / 3.0, "no cost carried: inverse hop count");
    o.pathTime = 5.0;
    c.check(seam.pheromone(o) == 0.2, "summed hop costs: their inverse");
    o.pathTime = 2.0;
    c.check(seam.pheromone(o) == 1.0 / 3.0, "a cost below the hop count counts as the hops");
    o.pathTime = kNaN;
    c.check(seam.pheromone(o) == 1.0 / 3.0, "a NaN cost counts as the hops");
    c.check(seam.pheromone(LinkObservation{}) == 0.0, "no path: no pheromone");
    LinkObservation unknownHops;
    unknownHops.hops = kNaN;
    c.check(seam.pheromone(unknownHops) == 0.0, "NaN hops and no cost: no pheromone");

    // The point of it: a short path through a drained relay ranks below a
    // longer one through full relays; with energy unknown everywhere, the
    // ranking is hop count's.
    const std::array<double, 2> drained{0.05, 1.0};  // 20 + 1
    const std::array<double, 3> full{1.0, 0.9, 1.0};
    LinkObservation viaDrained;
    viaDrained.hops = 2.0;
    viaDrained.pathTime = pathCost(m, drained);
    LinkObservation viaFull;
    viaFull.hops = 3.0;
    viaFull.pathTime = pathCost(m, full);
    c.check(viaDrained.pathTime == 21.0, "a drained relay dominates the path cost");
    c.check(m.pheromone(viaFull) > m.pheromone(viaDrained),
            "traffic shifts off the drained relay onto the longer path");
    const std::array<double, 2> unknown2{kNaN, kNaN};
    const std::array<double, 3> unknown3{kNaN, kNaN, kNaN};
    viaDrained.pathTime = pathCost(m, unknown2);
    viaFull.pathTime = pathCost(m, unknown3);
    c.check(m.pheromone(viaDrained) == 0.5 && m.pheromone(viaFull) == 1.0 / 3.0,
            "energy unknown: the metric degrades to hop count");

    // AntHocNet's metric, bit for bit, on a grid that crosses every branch.
    const std::array<double, 13> residuals{kNaN, -kInf, -1.0, 0.0,  0.005, 0.01, 0.0100001,
                                           0.1,  0.3,   0.5,  0.99, 1.0,   kInf};
    const std::array<double, 4> floors{0.01, 0.05, 0.2, 1.0};
    bool same = true;
    for (const double f : floors) {
        const EnergyMetric mf(f);
        for (const double e : residuals)
            same = same && sameBits(mf.hopCost(energy(e)), anthocnetHopCost(e, f)) &&
                   sameBits(energyHopCost(e, f), anthocnetHopCost(e, f));
    }
    const std::array<double, 7> values{kNaN, -1.0, 0.0, 0.5, 1.0, 3.0, 47.25};
    for (const double h : values) {
        for (const double p : values) {
            LinkObservation obs;
            obs.hops = h;
            obs.pathTime = p;
            same = same && sameBits(m.pheromone(obs), anthocnetPheromone(h, p));
        }
    }
    c.check(same, "the same bits as AntHocNet's EnergyMetric on every grid point");
}

}  // namespace swarm::core::test
