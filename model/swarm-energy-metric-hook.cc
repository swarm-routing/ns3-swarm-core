// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "swarm-energy-metric-hook.h"

#include "ns3/energy-source-container.h"
#include "ns3/log.h"
#include "ns3/node.h"

#include <algorithm>
#include <limits>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("EnergyMetricHook");
NS_OBJECT_ENSURE_REGISTERED(EnergyMetricHook);

TypeId EnergyMetricHook::GetTypeId() {
    static TypeId tid = TypeId("ns3::EnergyMetricHook")
                            .SetParent<Object>()
                            .SetGroupName("SwarmCore")
                            .AddConstructor<EnergyMetricHook>();
    return tid;
}

EnergyMetricHook::EnergyMetricHook() : m_initialJ(0.0), m_remainingJ(0.0), m_traced(false) {}

void EnergyMetricHook::DoDispose() {
    m_source = nullptr;
    Object::DoDispose();
}

void EnergyMetricHook::Install(Ptr<energy::EnergySource> source) {
    NS_ASSERT_MSG(!m_source, "EnergyMetricHook::Install called twice");
    NS_ASSERT(source);
    m_source = source;
    // GetInitialEnergy() is const and side-effect free; the source starts
    // full, so until its first update the remaining energy is the initial.
    m_initialJ = source->GetInitialEnergy();
    m_remainingJ = m_initialJ;
    m_traced = source->TraceConnectWithoutContext(
        "RemainingEnergy", MakeCallback(&EnergyMetricHook::NotifyRemaining, this));
    if (Ptr<Node> node = source->GetNode()) {
        node->AggregateObject(this);
    }
}

bool EnergyMetricHook::Install(Ptr<Node> node) {
    Ptr<energy::EnergySourceContainer> sources = node->GetObject<energy::EnergySourceContainer>();
    if (!sources || sources->GetN() == 0) {
        return false;
    }
    Install(sources->Get(0));
    return true;
}

void EnergyMetricHook::NotifyRemaining(double, double newJ) {
    m_remainingJ = newJ;
}

double EnergyMetricHook::residualEnergyFraction() const {
    if (!m_source || !m_traced || m_initialJ <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::clamp(m_remainingJ / m_initialJ, 0.0, 1.0);
}

}  // namespace ns3
