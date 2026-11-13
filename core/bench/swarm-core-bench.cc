// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Microbenchmarks of the per-packet primitives (#45, #46, swarm-routing/.github#32):
// the pheromone table's add, entries and row, roulette next-hop selection at
// 4, 16 and 64 neighbours, evaporation of a 64 x 16 table, and encoding and
// decoding a 40-byte header with the wire helpers. Dev-only: built
// with -DSWARM_CORE_BENCH=ON, linked against Google Benchmark (Apache-2.0,
// fetched at a pinned commit at configure time, never shipped). Run:
//
//   cmake -S core -B build/bench -DCMAKE_BUILD_TYPE=Release -DSWARM_CORE_BENCH=ON
//   cmake --build build/bench --target swarm_core_bench
//   build/bench/swarm_core_bench --benchmark_repetitions=5 --benchmark_report_aggregates_only
#include "../swarm-pheromone-table.h"
#include "../swarm-selection.h"
#include "../swarm-wire.h"
#include "../test/swarm-core-check.h"

#include <benchmark/benchmark.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using swarm::core::NodeAddress;
using swarm::core::PheromoneTable;
using swarm::core::WeightedHop;

/// `dests` destinations x `neighbors` neighbours, every value positive.
PheromoneTable filled(int dests, int neighbors) {
    PheromoneTable t(static_cast<std::size_t>(dests * neighbors));
    for (NodeAddress d = 0; d < dests; ++d) {
        for (NodeAddress n = 0; n < neighbors; ++n)
            t.set(d + 1000, n, 0.1 + 0.01 * ((d * 7 + n * 3) % 50));
    }
    return t;
}

// A deposit on an existing entry, cycling over a 64 x 16 table.
void BM_Add(benchmark::State& state) {
    PheromoneTable t = filled(64, 16);
    NodeAddress i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(t.add(1000 + (i & 63), (i >> 6) & 15, 1e-9));
        ++i;
    }
}
BENCHMARK(BM_Add);

// The allocating call every protocol uses today.
void BM_SelectNextHop(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    const PheromoneTable t = filled(1, n);
    swarm::core::test::SeededRng rng(1);
    for (auto _ : state)
        benchmark::DoNotOptimize(swarm::core::selectNextHop(t, 1000, 2.0, rng, 3));
}
BENCHMARK(BM_SelectNextHop)->Arg(4)->Arg(16)->Arg(64);

// The same selection with the caller's scratch buffer (#45).
void BM_SelectNextHopScratch(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    const PheromoneTable t = filled(1, n);
    swarm::core::test::SeededRng rng(1);
    std::vector<WeightedHop> scratch;
    for (auto _ : state)
        benchmark::DoNotOptimize(swarm::core::selectNextHop(t, 1000, 2.0, rng, scratch, 3));
}
BENCHMARK(BM_SelectNextHopScratch)->Arg(4)->Arg(16)->Arg(64);

// A full sweep of a 64 x 16 table. retain = 1 keeps the values (and the table)
// unchanged across iterations, so every iteration does the same work: it
// multiplies and compares every entry and removes none.
void BM_Evaporate(benchmark::State& state) {
    PheromoneTable t = filled(64, 16);
    // Read through a volatile so the compiler cannot fold retain = 1 away.
    // (DoNotOptimize on a double lvalue is not used here: with GCC it can
    // clobber the value.)
    volatile double one = 1.0;
    const double retain = one;
    for (auto _ : state)
        benchmark::DoNotOptimize(t.evaporate(retain, 1e-6));
    if (t.size() != 64 * 16) state.SkipWithError("the table lost entries: not a fixed workload");
}
BENCHMARK(BM_Evaporate);

// A copy of one 16-neighbour row out of a 64 x 16 table.
void BM_Entries(benchmark::State& state) {
    const PheromoneTable t = filled(64, 16);
    NodeAddress i = 0;
    for (auto _ : state) {
        auto e = t.entries(1000 + (i++ & 63));
        benchmark::DoNotOptimize(e.data());
    }
}
BENCHMARK(BM_Entries);

// The same row as a view (#45).
void BM_Row(benchmark::State& state) {
    const PheromoneTable t = filled(64, 16);
    NodeAddress i = 0;
    for (auto _ : state) {
        auto r = t.row(1000 + (i++ & 63));
        benchmark::DoNotOptimize(r.data());
    }
}
BENCHMARK(BM_Row);

// A 40-byte header (version, two addresses, a sequence number, a time, a hop
// count, a flag, a cost), as a protocol's control header writes it.
template <typename W> void WriteHeader(W& w, std::uint32_t seq) {
    w.version(1);
    w.i32(0x0A000001);
    w.i32(0x0A000002);
    w.u32(seq);
    w.f64(12.5);
    w.u16(3);
    w.boolean(true);
    w.f64(0.75);
    w.u64(0x1122334455667788ull);
    w.u8(9);
}

// Into a fresh vector each time: what every protocol's header does today.
void BM_WireEncode(benchmark::State& state) {
    std::uint32_t seq = 0;
    for (auto _ : state) {
        std::vector<std::uint8_t> bytes;
        swarm::core::WireWriter w(bytes);
        WriteHeader(w, ++seq);
        benchmark::DoNotOptimize(bytes.data());
    }
}
BENCHMARK(BM_WireEncode);

// BEGIN-NEW-WIRE (#46: not in the writer before it)
void BM_WireEncodeReserved(benchmark::State& state) {
    std::uint32_t seq = 0;
    for (auto _ : state) {
        std::vector<std::uint8_t> bytes;
        swarm::core::WireWriter w(bytes);
        w.reserve(40);
        WriteHeader(w, ++seq);
        benchmark::DoNotOptimize(bytes.data());
    }
}
BENCHMARK(BM_WireEncodeReserved);

void BM_WireEncodeFixed(benchmark::State& state) {
    std::uint32_t seq = 0;
    for (auto _ : state) {
        swarm::core::FixedWireWriter<40> w;
        WriteHeader(w, ++seq);
        benchmark::DoNotOptimize(w.bytes().data());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_WireEncodeFixed);
// END-NEW-WIRE

void BM_WireDecode(benchmark::State& state) {
    std::vector<std::uint8_t> bytes;
    swarm::core::WireWriter w(bytes);
    WriteHeader(w, 7);
    for (auto _ : state) {
        swarm::core::WireReader r(bytes);
        r.expectVersion(1);
        std::uint64_t acc =
            static_cast<std::uint32_t>(r.i32()) + static_cast<std::uint32_t>(r.i32());
        acc += r.u32();
        acc += std::bit_cast<std::uint64_t>(r.f64());
        acc += r.u16();
        acc += r.boolean() ? 1 : 0;
        acc += std::bit_cast<std::uint64_t>(r.f64());
        acc += r.u64() + r.u8();
        benchmark::DoNotOptimize(acc);
        benchmark::DoNotOptimize(r.atEnd());
    }
}
BENCHMARK(BM_WireDecode);

}  // namespace

BENCHMARK_MAIN();
