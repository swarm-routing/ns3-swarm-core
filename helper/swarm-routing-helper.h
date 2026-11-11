// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Helpers that install a swarm-routing protocol and its link monitors. New
// here; the shape follows ns-3's routing helpers (AodvHelper, OlsrHelper) and
// swarm-routing/AntHocNet ns3/anthocnet/helper/anthocnet-helper.{h,cc}
// @ 3e53f04 (GPL-2.0-only).
//
//   SwarmRoutingHelper ara("ns3::ara::RoutingProtocol");
//   ara.Set("SomeAttribute", ...);
//   ara.Install(nodes);           // IPv4 stack + the protocol + every monitor that applies
//   ...assign addresses...
//   ara.AssignStreams(nodes, 0);  // before Simulator::Run
//   ...Simulator::Run...
//   SwarmRoutingStats stats = SwarmRoutingHelper::CollectStats(nodes);  // the observer's counters
#ifndef SWARM_ROUTING_HELPER_H
#define SWARM_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/swarm-observer.h"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ns3 {

/// Installs the monitors that apply to a node: a WifiLinkMonitor when it has a
/// wifi device, an IslLinkMonitor when it has a point-to-point device, an
/// EnergyMetricHook when an energy source is aggregated to it. A monitor the
/// node already has is kept, so calling it again (say, after the energy
/// sources are installed) only adds what is missing.
class SwarmMonitorHelper {
public:
    /// Returns the number of monitors installed.
    uint32_t Install(Ptr<Node> node) const;
    uint32_t Install(const NodeContainer& nodes) const;
};

/// The router observer's counters (core/swarm-observer.h) of a set of nodes.
struct SwarmRoutingStats {
    /// (node id, its counters), in the container's order; nodes without a
    /// SwarmRoutingBase protocol are left out.
    std::vector<std::pair<uint32_t, swarm::core::RouterStats>> perNode;
    /// Every node's counters merged (RouterStats::merge: footprints sum).
    swarm::core::RouterStats aggregate;
};

/// An Ipv4RoutingHelper for any protocol derived from SwarmRoutingBase, named
/// by its TypeId.
class SwarmRoutingHelper : public Ipv4RoutingHelper {
public:
    explicit SwarmRoutingHelper(const std::string& protocolTypeId);

    SwarmRoutingHelper* Copy() const override;
    /// Creates the protocol and aggregates it to `node` (InternetStackHelper
    /// calls this; so does Install).
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    /// Sets an attribute of every protocol instance created from now on.
    void Set(const std::string& name, const AttributeValue& value);

    /// The one call: an IPv4 stack with this protocol on every node, then
    /// SwarmMonitorHelper::Install.
    void Install(const NodeContainer& nodes) const;

    /// Pins the randomness of the protocol on every node, from `stream`
    /// upwards; returns the number of streams used.
    int64_t AssignStreams(const NodeContainer& nodes, int64_t stream) const;

    /// Names this protocol's control kinds for readers (its static KindName
    /// table; SwarmRoutingBase::SetKindNames on the helper's TypeId).
    void SetKindNames(std::span<const swarm::core::KindName> names) const;
    /// The name of `kind` for this protocol, else the number in decimal.
    std::string KindName(uint8_t kind) const;

    /// The router observer's per-node and aggregate counters, time averages
    /// taken up to now (call it after Simulator::Run, before Destroy).
    static SwarmRoutingStats CollectStats(const NodeContainer& nodes);

private:
    ObjectFactory m_factory;
};

}  // namespace ns3

#endif  // SWARM_ROUTING_HELPER_H
