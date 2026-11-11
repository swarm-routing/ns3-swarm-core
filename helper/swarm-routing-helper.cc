// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "swarm-routing-helper.h"

#include "ns3/swarm-energy-metric-hook.h"
#include "ns3/swarm-isl-link-monitor.h"
#include "ns3/swarm-routing-base.h"
#include "ns3/swarm-wifi-link-monitor.h"

#include "ns3/abort.h"
#include "ns3/energy-source-container.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/node.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/wifi-net-device.h"

namespace ns3 {

namespace {

bool HasDevice(Ptr<Node> node, TypeId tid) {
    for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
        // IsChildOf excludes the type itself.
        const TypeId t = node->GetDevice(i)->GetInstanceTypeId();
        if (t == tid || t.IsChildOf(tid)) {
            return true;
        }
    }
    return false;
}

}  // namespace

uint32_t SwarmMonitorHelper::Install(Ptr<Node> node) const {
    uint32_t installed = 0;
    if (!node->GetObject<WifiLinkMonitor>() && HasDevice(node, WifiNetDevice::GetTypeId())) {
        CreateObject<WifiLinkMonitor>()->Install(node);
        ++installed;
    }
    if (!node->GetObject<IslLinkMonitor>() && HasDevice(node, PointToPointNetDevice::GetTypeId())) {
        CreateObject<IslLinkMonitor>()->Install(node);
        ++installed;
    }
    if (!node->GetObject<EnergyMetricHook>() && CreateObject<EnergyMetricHook>()->Install(node)) {
        ++installed;
    }
    return installed;
}

uint32_t SwarmMonitorHelper::Install(const NodeContainer& nodes) const {
    uint32_t installed = 0;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it) {
        installed += Install(*it);
    }
    return installed;
}

SwarmRoutingHelper::SwarmRoutingHelper(const std::string& protocolTypeId) {
    m_factory.SetTypeId(protocolTypeId);
    NS_ABORT_MSG_UNLESS(m_factory.GetTypeId().IsChildOf(SwarmRoutingBase::GetTypeId()),
                        protocolTypeId << " does not derive from ns3::SwarmRoutingBase");
}

SwarmRoutingHelper* SwarmRoutingHelper::Copy() const {
    return new SwarmRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol> SwarmRoutingHelper::Create(Ptr<Node> node) const {
    Ptr<SwarmRoutingBase> protocol = m_factory.Create<SwarmRoutingBase>();
    node->AggregateObject(protocol);
    return protocol;
}

void SwarmRoutingHelper::Set(const std::string& name, const AttributeValue& value) {
    m_factory.Set(name, value);
}

void SwarmRoutingHelper::Install(const NodeContainer& nodes) const {
    InternetStackHelper stack;
    stack.SetRoutingHelper(*this);
    stack.Install(nodes);
    SwarmMonitorHelper().Install(nodes);
}

int64_t SwarmRoutingHelper::AssignStreams(const NodeContainer& nodes, int64_t stream) const {
    int64_t used = 0;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it) {
        if (Ptr<SwarmRoutingBase> protocol = (*it)->GetObject<SwarmRoutingBase>()) {
            used += protocol->AssignStreams(stream + used);
        }
    }
    return used;
}

void SwarmRoutingHelper::SetKindNames(std::span<const swarm::core::KindName> names) const {
    SwarmRoutingBase::SetKindNames(m_factory.GetTypeId(), names);
}

std::string SwarmRoutingHelper::KindName(uint8_t kind) const {
    return SwarmRoutingBase::GetKindName(m_factory.GetTypeId(), kind);
}

SwarmRoutingStats SwarmRoutingHelper::CollectStats(const NodeContainer& nodes) {
    SwarmRoutingStats out;
    for (auto it = nodes.Begin(); it != nodes.End(); ++it) {
        if (Ptr<SwarmRoutingBase> protocol = (*it)->GetObject<SwarmRoutingBase>()) {
            out.perNode.emplace_back((*it)->GetId(), protocol->GetRouterStats());
            out.aggregate.merge(out.perNode.back().second);
        }
    }
    return out;
}

}  // namespace ns3
