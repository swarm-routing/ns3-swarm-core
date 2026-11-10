// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The bounded structures (golden rule 7): the visited path, the dedup history
// and the pheromone table each refuse to grow past their cap.
#include "../swarm-dedup-history.h"
#include "../swarm-pheromone-table.h"
#include "../swarm-selection.h"
#include "../swarm-visited-path.h"
#include "swarm-core-tests.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace swarm::core::test {

void TestVisitedPath(Checker& c) {
    VisitedPath p(3);
    c.check(p.empty() && p.capacity() == 3 && !p.full(), "new path is empty");
    c.check(p.back().node == kInvalidAddress, "back() of an empty path is invalid");
    c.check(p.push(10, 0.5) && p.push(11) && p.push(12, 1.5), "push up to the cap");
    c.check(p.full() && p.size() == 3, "full at the cap");
    c.check(!p.push(13), "push past the cap is refused");
    c.check(p.size() == 3 && !p.contains(13), "a refused push changes nothing");
    c.check(p.contains(11) && !p.contains(99), "contains");
    c.check(p.back() == Hop{12, 1.5}, "back");
    c.check(p.hops()[0] == Hop{10, 0.5} && p.hops()[1].time == 0.0, "hops in order");
    p.pop();
    c.check(p.size() == 2 && p.back().node == 11 && !p.full(), "pop");
    p.clear();
    p.pop();
    c.check(p.empty(), "clear, and pop on empty is a no-op");

    c.check(VisitedPath(0).capacity() == 1, "capacity 0 is raised to 1, never unbounded");
    c.check(VisitedPath(1u << 20).capacity() == VisitedPath::kMaxCapacity,
            "capacity is clamped to what the wire carries");
}

void TestVisitedPathWire(Checker& c) {
    VisitedPath p(4);
    p.push(1, 0.25);
    p.push(-7, 3.0);
    std::vector<std::uint8_t> bytes;
    WireWriter w(bytes);
    p.write(w);
    c.check(bytes.size() == 2 + 2 * VisitedPath::kHopWireSize, "encoded size");

    WireReader r(bytes);
    VisitedPath back(4);
    c.check(VisitedPath::read(r, back) && r.atEnd() && back == p, "path round-trips");

    // A receiver with a smaller cap rejects the longer path, and is left empty.
    WireReader rSmall(bytes);
    VisitedPath small(1);
    small.push(5);
    c.check(!VisitedPath::read(rSmall, small) && small.empty(), "over-cap path is rejected");

    // A truncated hop is rejected.
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 1);
    WireReader rCut(cut);
    VisitedPath out(4);
    c.check(!VisitedPath::read(rCut, out) && out.empty(), "truncated path is rejected");
}

void TestDedupHistory(Checker& c) {
    DedupHistory h(3);
    c.check(h.capacity() == 3 && h.size() == 0, "new history");
    c.check(h.record(1, 1) && h.record(1, 2) && h.record(2, 1), "new entries");
    c.check(!h.record(1, 2), "a duplicate is reported");
    c.check(h.seen(1, 1) && !h.seen(3, 3), "seen");
    c.check(h.record(4, 4) && h.size() == 3, "the cap holds");
    c.check(!h.seen(1, 1) && h.seen(4, 4), "the oldest entry is evicted first");
    // A duplicate does not refresh its age: (1, 2) is next out.
    c.check(h.record(5, 5) && !h.seen(1, 2) && h.seen(2, 1), "FIFO, not LRU");
    h.clear();
    c.check(h.size() == 0 && !h.seen(5, 5), "clear");
    DedupHistory one(0);
    c.check(one.capacity() == 1, "capacity 0 is raised to 1, never unbounded");
    one.record(1, 1);
    one.record(1, 2);
    c.check(one.size() == 1 && one.seen(1, 2), "capacity 1 keeps the newest");
}

void TestPheromoneTable(Checker& c) {
    PheromoneTable t(4);
    c.check(t.empty() && t.capacity() == 4, "new table");
    c.check(t.set(9, 2, 0.5) && t.set(9, 1, 0.25) && t.set(8, 1, 1.0), "set");
    c.check(t.get(9, 2) == 0.5 && t.get(9, 3) == 0.0, "get, 0 when absent");
    c.check(t.contains(8, 1) && !t.contains(8, 2), "contains");
    c.check(t.best(9) == 0.5 && t.best(7) == 0.0, "best");
    c.check(t.add(9, 1, 0.5) && t.get(9, 1) == 0.75, "add to an entry");
    c.check(t.add(7, 3, 0.125) && t.size() == 4 && t.full(), "add creates an entry");
    c.check(!t.set(6, 6, 1.0) && !t.contains(6, 6), "a new entry past the cap is refused");
    c.check(t.set(9, 2, 2.0) && t.get(9, 2) == 2.0, "updating an entry at the cap works");
    c.check(!t.set(9, 2, std::numeric_limits<double>::infinity()) && t.get(9, 2) == 2.0,
            "a non-finite value is refused");
    c.check(!t.set(9, 2, std::nan("")) && t.get(9, 2) == 2.0, "NaN is refused");
    c.check(!t.add(9, 2, std::numeric_limits<double>::max()) || std::isfinite(t.get(9, 2)),
            "add never stores a non-finite value");

    const auto e = t.entries(9);
    c.check(e.size() == 2 && e[0].neighbor == 1 && e[1].neighbor == 2, "entries ascending");
    c.check(t.entries(5).empty(), "no entries for an unknown destination");
    c.check(t.destinations() == std::vector<NodeAddress>{7, 8, 9}, "destinations ascending");

    c.check(t.eraseNeighbor(1) == 2 && !t.contains(9, 1) && !t.contains(8, 1), "eraseNeighbor");
    c.check(t.erase(9, 2) && !t.erase(9, 2), "erase");
    c.check(t.eraseDestination(7) == 1 && t.empty(), "eraseDestination");
    t.set(1, 1, 1.0);
    t.clear();
    c.check(t.empty(), "clear");
    c.check(PheromoneTable(0).capacity() == 1, "capacity 0 is raised to 1, never unbounded");

    // Negative addresses order before positive ones and are found too.
    PheromoneTable n(4);
    n.set(-5, -1, 1.0);
    n.set(-5, 3, 2.0);
    c.check(n.entries(-5).size() == 2 && n.best(-5) == 2.0, "negative addresses");
}

void TestEvaporation(Checker& c) {
    PheromoneTable t(8);
    t.set(1, 1, 1.0);
    t.set(1, 2, 0.1);
    t.set(2, 1, 0.5);
    c.check(t.evaporate(0.5, 0.06) == 1, "one entry falls below the floor");
    c.check(t.get(1, 1) == 0.5 && t.get(2, 1) == 0.25 && !t.contains(1, 2), "values aged");
    c.check(t.evaporate(1.5, 0.0) == 0 && t.get(1, 1) == 0.5, "retain > 1 changes nothing");
    c.check(t.evaporate(std::nan(""), 0.0) == 0 && t.get(1, 1) == 0.5, "NaN changes nothing");
    c.check(t.evaporate(0.0, 1e-9) == 2 && t.empty(), "retain 0 empties the table");

    c.check(evaporationFactor(0.5, 2.0, 1.0) == 0.25, "two intervals");
    c.check(evaporationFactor(0.25, 0.5, 1.0) == 0.5, "half an interval");
    c.check(evaporationFactor(0.5, 0.0, 1.0) == 1.0, "no time, no ageing");
    c.check(evaporationFactor(0.5, 1.0, 0.0) == 1.0, "no interval, no ageing");
    c.check(evaporationFactor(1.5, 1.0, 1.0) == 1.0, "retain outside [0, 1] is ignored");
    // Ageing in two steps equals ageing once for the sum of the times.
    const double once = evaporationFactor(0.9, 3.0, 1.0);
    const double twice = evaporationFactor(0.9, 1.0, 1.0) * evaporationFactor(0.9, 2.0, 1.0);
    c.check(std::fabs(once - twice) < 1e-12, "time-proportional");
}

}  // namespace swarm::core::test
