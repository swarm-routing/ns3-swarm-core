// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// SwarmLifecycleHelper: scripts node shutdowns, crashes and restarts at given
// simulated times, identically for every arm of a comparison (#43). After
// INET's LifecycleController and its <shutdown>/<crash>/<startup> scenario
// elements: each operation completes in zero simulated time.
//
// For a node running a SwarmRoutingBase protocol the helper calls its
// Shutdown/Crash/Start (the hooks, the timers, the deferred queue); for every
// node, swarm or stock arm alike, it also takes the node's routed IPv4
// interfaces down (Ipv4::SetDown) and back up (SetUp), so a stock protocol in
// the same scenario sees the same failure through ns-3's own path.
//
//   SwarmLifecycleHelper life;
//   life.Schedule(Seconds(20), nodes.Get(3), SwarmLifecycleOp::Crash);
//   life.Schedule(Seconds(40), nodes.Get(3), SwarmLifecycleOp::Start);
//   life.ScheduleFile("failures.txt", nodes);   // the same, as data
//
// A schedule file has one operation per line, `<time s> <node> <op>`, where
// <node> indexes the NodeContainer given and <op> is start, shutdown or
// crash; blank lines and `#` comments are skipped. Operations at the same
// time run in file order (the simulator runs same-time events in the order
// they were scheduled), so a fixed seed and a fixed file give byte-identical
// output (org rule 5). The file is read once, at most MaxEntries lines of
// operations (org rule 7).
#ifndef SWARM_LIFECYCLE_HELPER_H
#define SWARM_LIFECYCLE_HELPER_H

#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/swarm-routing-base.h"

#include <cstdint>
#include <string>

namespace ns3 {

class SwarmLifecycleHelper {
public:
    /// The most operations one schedule file may hold.
    static constexpr uint32_t kMaxEntries = 100000;

    /// Runs `op` on `node` at simulated time `at` (absolute).
    void Schedule(Time at, Ptr<Node> node, SwarmLifecycleOp op) const;
    /// The same for every node of `nodes`, in container order.
    void Schedule(Time at, const NodeContainer& nodes, SwarmLifecycleOp op) const;

    /// Reads a schedule file (format above) and schedules every operation.
    /// Aborts on a malformed line, an index outside `nodes`, a negative
    /// time, or more than kMaxEntries operations. Returns the operations
    /// scheduled.
    uint32_t ScheduleFile(const std::string& path, const NodeContainer& nodes) const;

    /// Runs `op` on `node` now: the protocol's lifecycle when it is a
    /// SwarmRoutingBase, and the node's routed interfaces down or up.
    static void Apply(Ptr<Node> node, SwarmLifecycleOp op);

    /// "start", "shutdown" or "crash"; false for anything else.
    static bool ParseOp(const std::string& name, SwarmLifecycleOp& op);
};

}  // namespace ns3

#endif  // SWARM_LIFECYCLE_HELPER_H
