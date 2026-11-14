// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The scratch consumer of core/ (#100, CMakeLists.txt beside this file): it
// calls one function compiled into swarm_core, so the link is real, and runs
// the header-only testbench (#15) as a protocol repository's core test would.
#include "swarm-selection.h"
#include "testbench/swarm-testbench.h"

#include <array>
#include <cstdio>
#include <memory>

namespace {

/// Rebroadcasts the first message it hears: a flood over the testbench.
struct Flood final : swarm::core::testbench::INodeAdapter<int> {
    explicit Flood(swarm::core::testbench::NodePorts<int>& p) : ports(p) {}
    void start() override {
        if (ports.address() == 0) {
            heard = true;
            ports.broadcast(1);
        }
    }
    void receive(swarm::core::NodeAddress, const int& m) override {
        if (!heard) {
            heard = true;
            ports.broadcast(m);
        }
    }
    swarm::core::testbench::NodePorts<int>& ports;
    bool heard = false;
};

}  // namespace

int main() {
    using namespace swarm::core;
    const std::array<WeightedHop, 2> hops{WeightedHop{1, 0.0}, WeightedHop{2, 1.0}};
    // An old-style cast, on purpose: core/'s own build rejects it
    // (-Wold-style-cast -Werror). It compiles here only because those flags
    // stay on swarm-core's build and do not reach a consumer's sources.
    const double weight = (double)hops.size();
    // With no positive weight the function returns kInvalidAddress; with one,
    // that candidate wins whatever the draw.
    const std::array<WeightedHop, 1> none{WeightedHop{1, 0.0}};
    struct NoRng final : IRng {
        double uniform() override { return 0.0; }
        int uniformInt(int) override { return 0; }
    } rng;
    namespace tb = swarm::core::testbench;
    tb::Network<int> net(tb::Topology::line(4));
    net.install([](tb::NodePorts<int>& p) { return std::make_unique<Flood>(p); });
    net.run(1.0);
    const bool ok = weight == 2.0 && rouletteSelect(none, rng) == kInvalidAddress &&
                    rouletteSelect(hops, rng) == 2 && net.sentTotal() == 4;
    std::puts(ok ? "consumer: ok" : "consumer: FAILED");
    return ok ? 0 : 1;
}
