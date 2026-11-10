// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// EnergyMetricHook: a node's residual battery, from an ns3::energy::EnergySource,
// as the core's IEnergy port (the HopSignals::residualEnergy an energy-aware
// metric reads; #2, AntHocNet #145 and #801). New here: AntHocNet's adapter
// read a probe callback a harness set.
//
// Non-intrusive by construction. EnergySource::GetRemainingEnergy() is not a
// read: it brings the source up to date and reschedules its next update event,
// so calling it would change the event sequence, and the depletion time, of
// the run being measured. The hook listens to the source's "RemainingEnergy"
// trace (a TracedValue on BasicEnergySource, LiIonEnergySource and the others)
// instead, and reports the value as of the source's last update (its
// PeriodicEnergyUpdateInterval, 1 s by default). A source without that trace
// reads NaN, the port's "no evidence".
#ifndef SWARM_ENERGY_METRIC_HOOK_H
#define SWARM_ENERGY_METRIC_HOOK_H

#include "ns3/swarm-ports.h"

#include "ns3/energy-source.h"
#include "ns3/object.h"

namespace ns3 {

class Node;

class EnergyMetricHook : public Object, public swarm::core::IEnergy {
public:
    static TypeId GetTypeId();
    EnergyMetricHook();

    /// Listens to `source` and aggregates itself to the source's node.
    void Install(Ptr<energy::EnergySource> source);
    /// Install() on the node's first energy source (the EnergySourceContainer
    /// energy helpers aggregate to it). Returns false, and installs nothing,
    /// when the node has none.
    bool Install(Ptr<Node> node);

    /// Remaining / initial energy of the source at its last update, clamped to
    /// 0..1; NaN with no source, a source without the trace, or zero initial
    /// energy.
    double residualEnergyFraction() const override;

protected:
    void DoDispose() override;

private:
    void NotifyRemaining(double oldJ, double newJ);

    Ptr<energy::EnergySource> m_source;
    double m_initialJ;
    double m_remainingJ;
    bool m_traced;
};

}  // namespace ns3

#endif  // SWARM_ENERGY_METRIC_HOOK_H
