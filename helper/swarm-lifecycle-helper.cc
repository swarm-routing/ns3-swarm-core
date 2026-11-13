// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "swarm-lifecycle-helper.h"

#include "ns3/abort.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <fstream>
#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SwarmLifecycleHelper");

namespace {

Ptr<SwarmRoutingBase> SwarmProtocolOf(Ptr<Node> node) {
    if (Ptr<SwarmRoutingBase> p = node->GetObject<SwarmRoutingBase>()) {
        return p;  // aggregated, as SwarmRoutingHelper does
    }
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    return ipv4 ? DynamicCast<SwarmRoutingBase>(ipv4->GetRoutingProtocol()) : nullptr;
}

/// Interface 0 is the loopback; every other one is routed.
void SetInterfaces(Ptr<Node> node, bool up) {
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4) {
        return;
    }
    for (uint32_t i = 1; i < ipv4->GetNInterfaces(); ++i) {
        if (up && !ipv4->IsUp(i)) {
            ipv4->SetUp(i);
        } else if (!up && ipv4->IsUp(i)) {
            ipv4->SetDown(i);
        }
    }
}

}  // namespace

bool SwarmLifecycleHelper::ParseOp(const std::string& name, SwarmLifecycleOp& op) {
    if (name == "start") {
        op = SwarmLifecycleOp::Start;
    } else if (name == "shutdown") {
        op = SwarmLifecycleOp::Shutdown;
    } else if (name == "crash") {
        op = SwarmLifecycleOp::Crash;
    } else {
        return false;
    }
    return true;
}

void SwarmLifecycleHelper::Apply(Ptr<Node> node, SwarmLifecycleOp op) {
    NS_LOG_FUNCTION(node->GetId() << static_cast<int>(op));
    Ptr<SwarmRoutingBase> protocol = SwarmProtocolOf(node);
    switch (op) {
    case SwarmLifecycleOp::Shutdown:
        if (protocol) {
            protocol->Shutdown();  // the leaving control goes out before the interfaces go
        }
        SetInterfaces(node, false);
        break;
    case SwarmLifecycleOp::Crash:
        if (protocol) {
            protocol->Crash();
        }
        SetInterfaces(node, false);
        break;
    case SwarmLifecycleOp::Start:
        SetInterfaces(node, true);  // first: the protocol's sockets come back with them
        if (protocol) {
            protocol->Start();
        }
        break;
    }
}

void SwarmLifecycleHelper::Schedule(Time at, Ptr<Node> node, SwarmLifecycleOp op) const {
    NS_ABORT_MSG_IF(at < Simulator::Now(), "a lifecycle operation in the past");
    Simulator::Schedule(at - Simulator::Now(), &SwarmLifecycleHelper::Apply, node, op);
}

void SwarmLifecycleHelper::Schedule(Time at, const NodeContainer& nodes,
                                    SwarmLifecycleOp op) const {
    for (auto it = nodes.Begin(); it != nodes.End(); ++it) {
        Schedule(at, *it, op);
    }
}

uint32_t SwarmLifecycleHelper::ScheduleFile(const std::string& path,
                                            const NodeContainer& nodes) const {
    std::ifstream in(path);
    NS_ABORT_MSG_UNLESS(in, "cannot read the lifecycle schedule " << path);
    std::string line;
    uint32_t lineNo = 0;
    uint32_t scheduled = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream fields(line);
        double seconds = 0.0;
        uint32_t index = 0;
        std::string name;
        if (!(fields >> seconds)) {
            continue;  // blank or comment-only
        }
        std::string rest;
        SwarmLifecycleOp op = SwarmLifecycleOp::Start;
        NS_ABORT_MSG_UNLESS((fields >> index >> name) && !(fields >> rest) && ParseOp(name, op),
                            path << ":" << lineNo << ": expected `<time s> <node> <op>`");
        NS_ABORT_MSG_UNLESS(seconds >= 0.0, path << ":" << lineNo << ": negative time");
        NS_ABORT_MSG_UNLESS(index < nodes.GetN(),
                            path << ":" << lineNo << ": node " << index << " not in the container");
        NS_ABORT_MSG_UNLESS(scheduled < kMaxEntries,
                            path << ": more than " << kMaxEntries << " operations");
        Schedule(Seconds(seconds), nodes.Get(index), op);
        ++scheduled;
    }
    return scheduled;
}

}  // namespace ns3
