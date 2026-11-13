// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Dev-only microbenchmark of the deferred queue (#46): the cost of draining a
// full queue through SwarmRoutingBase::ReleaseDeferred when no route is found
// (every packet goes back), and of releasing one destination's packets out of
// a mixed queue, at 64 and 1024 entries. Wall-clock timing is for this
// program's report only; nothing it measures reaches a simulation's output.
// Not run by CI. Run:
//
//   ./ns3 run swarm-core-queue-bench
#include "ns3/core-module.h"
#include "ns3/swarm-core-module.h"

#include <chrono>
#include <cstdio>

using namespace ns3;

namespace {

class BenchProtocol : public SwarmRoutingBase {
public:
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("ns3::SwarmQueueBenchProtocol")
                                .SetParent<SwarmRoutingBase>()
                                .AddConstructor<BenchProtocol>();
        return tid;
    }

    void Hold(Ipv4Address dst, Ptr<const Packet> p) {
        Ipv4Header h;
        h.SetDestination(dst);
        h.SetSource(Ipv4Address("10.0.0.1"));
        Defer(p, h, UnicastForwardCallback(), ErrorCallback());
    }
    void Release(Ipv4Address dst) { ReleaseDeferred(dst); }

    Ipv4Address route;  ///< what SelectNextHop answers (none by default)

protected:
    uint16_t ControlPort() const override { return 9; }
    Ipv4Address SelectNextHop(Ipv4Address, Ipv4Address, Ipv4Address, Ptr<const Packet>) override {
        return route;
    }
    void StartDiscovery(Ipv4Address) override {}
    void RecvControl(Ptr<Packet>, Ipv4Address, Ipv4Address) override {}
};

template <typename F> double TimeNs(int reps, F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) {
        f();
    }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / reps;
}

}  // namespace

int main(int argc, char* argv[]) {
    int reps = 20;
    CommandLine cmd(__FILE__);
    cmd.AddValue("reps", "repetitions per measurement", reps);
    cmd.Parse(argc, argv);

    const Ptr<const Packet> pkt = Create<Packet>(64);
    for (uint32_t n : {64u, 1024u}) {
        Ptr<BenchProtocol> proto = CreateObject<BenchProtocol>();
        proto->GetDeferredQueue()->SetAttribute("MaxPackets", UintegerValue(n));

        // Drain with no route: every packet is released and goes back.
        for (uint32_t i = 0; i < n; ++i) {
            proto->Hold(Ipv4Address("10.0.0.9"), pkt);
        }
        const double drain = TimeNs(reps, [&] { proto->Release(Ipv4Address("10.0.0.9")); });

        // A mixed queue of 8 destinations; release one with a route found.
        Ptr<BenchProtocol> mixed = CreateObject<BenchProtocol>();
        mixed->GetDeferredQueue()->SetAttribute("MaxPackets", UintegerValue(n));
        mixed->route = Ipv4Address("10.0.0.2");
        const double release = TimeNs(reps, [&] {
            for (uint32_t i = 0; i < n; ++i) {
                mixed->Hold(Ipv4Address(0x0a000100 + (i % 8)), pkt);
            }
            for (uint32_t d = 0; d < 8; ++d) {
                mixed->Release(Ipv4Address(0x0a000100 + d));
            }
        });
        std::printf("entries=%u drain_no_route_ns=%.0f fill_and_release_8_dests_ns=%.0f\n", n,
                    drain, release);
        proto->Dispose();
        mixed->Dispose();
    }
    Simulator::Destroy();
    return 0;
}
