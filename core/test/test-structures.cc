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
#include <algorithm>
#include <limits>
#include <map>
#include <utility>
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

void TestDedupHistoryLifetime(Checker& c) {
    // No max age: the time-aware calls behave as the plain ones (#33).
    DedupHistory plain(2);
    c.check(!plain.maxAge() && plain.record(1, 1, 0.0), "no age: record");
    c.check(plain.seen(1, 1, 1e9) && !plain.record(1, 1, 1e9), "no age: an entry never expires");
    c.check(plain.purge(1e9) == 0 && plain.size() == 1, "no age: purge is a no-op");

    DedupHistory h(4, 30.0);
    c.check(h.maxAge() && *h.maxAge() == 30.0, "max age kept");
    c.check(h.record(1, 1, 10.0) && h.record(2, 1, 20.0), "timed records");
    c.check(!h.record(1, 1, 39.999) && h.seen(1, 1, 39.999), "live until just before the age");
    c.check(!h.seen(1, 1, 40.0) && h.seen(1, 1),
            "expired at exactly the age; plain seen ignores it");
    c.check(h.size() == 2, "an expired entry stays until purged");
    c.check(h.purge(40.0) == 1 && !h.seen(1, 1) && h.seen(2, 1, 40.0), "purge forgets the expired");
    c.check(h.purge(40.0) == 0, "purge is idempotent");

    // A fractional boundary: 1.1 + 0.2 is expired at exactly 1.1 + 0.2, even
    // though (1.1 + 0.2) - 1.1 rounds below 0.2.
    DedupHistory frac(2, 0.2);
    frac.record(9, 9, 1.1);
    c.check(frac.seen(9, 9, 1.1 + 0.19) && !frac.seen(9, 9, 1.1 + 0.2), "fractional boundary");
    c.check(frac.record(9, 9, 1.1 + 0.2) && frac.purge(1.1 + 0.2 + 0.2) == 1,
            "re-recorded, purged");

    // An expired entry recorded again is new, and becomes the newest (FIFO).
    DedupHistory r(2, 5.0);
    r.record(7, 1, 0.0);
    r.record(7, 2, 3.0);
    c.check(r.record(7, 1, 5.0) && r.size() == 2, "an expired duplicate is recorded afresh");
    c.check(r.record(7, 3, 6.0) && !r.seen(7, 2) && r.seen(7, 1, 6.0), "it moved to the back");

    // The count cap and the age together: the cap evicts the oldest first even
    // when nothing has expired; the age frees entries before the cap does.
    DedupHistory both(3, 10.0);
    both.record(1, 1, 0.0);
    both.record(1, 2, 1.0);
    both.record(1, 3, 2.0);
    c.check(both.record(1, 4, 3.0) && !both.seen(1, 1) && both.size() == 3, "the cap still holds");
    c.check(both.purge(12.5) == 2 && both.size() == 1 && both.seen(1, 4, 12.5),
            "the age frees room");
    c.check(both.record(2, 1, 13.0) && both.record(2, 2, 13.0) && both.size() == 3,
            "room is reused");

    // Untimed records never expire, even with an age; purge keeps FIFO order.
    DedupHistory mixed(4, 1.0);
    mixed.record(1, 1);
    mixed.record(1, 2, 0.0);
    mixed.record(1, 3);
    c.check(mixed.purge(5.0) == 1 && mixed.seen(1, 1, 5.0) && mixed.seen(1, 3, 5.0),
            "untimed stay");
    mixed.record(1, 4);
    mixed.record(1, 5);
    mixed.record(1, 6);
    c.check(!mixed.seen(1, 1) && mixed.seen(1, 3), "order kept across a purge");

    // A non-finite age is no age; a negative one is 0 (expired at once).
    c.check(!DedupHistory(2, std::numeric_limits<double>::infinity()).maxAge(), "inf age is none");
    DedupHistory zero(2, -1.0);
    c.check(zero.record(3, 3, 1.0) && !zero.seen(3, 3, 1.0), "negative age reads as 0");

    // clear() resets to the freshly constructed state (#43): cap and age kept.
    h.clear();
    c.check(h.size() == 0 && h.capacity() == 4 && h.maxAge() && *h.maxAge() == 30.0,
            "clear keeps config");
}

void TestResetContract(Checker& c) {
    // The reset contract (#43): clear() returns each shared primitive to its
    // freshly constructed state, configuration kept, so a protocol core can
    // forget everything in one call on a node shutdown or crash.
    VisitedPath p(3);
    p.push(1);
    p.push(2);
    p.clear();
    c.check(p.empty() && p.capacity() == 3 && p == VisitedPath(3), "visited path: as new");
    c.check(p.push(4) && p.push(5) && p.push(6) && !p.push(7), "visited path: cap kept");

    DedupHistory h(2, 10.0);
    h.record(1, 1, 0.0);
    h.record(1, 2);
    h.clear();
    c.check(h.size() == 0 && !h.seen(1, 1) && !h.seen(1, 2), "dedup: forgets every entry");
    c.check(h.capacity() == 2 && h.maxAge() && *h.maxAge() == 10.0, "dedup: cap and age kept");
    c.check(h.record(1, 1, 0.0) && !h.seen(1, 1, 10.0), "dedup: the age still applies");

    PheromoneTable t(2);
    t.set(1, 1, 0.5);
    t.set(1, 2, 0.25);
    t.clear();
    c.check(t.empty() && t.capacity() == 2 && t.destinations().empty(), "pheromone: as new");
    c.check(t.set(2, 1, 1.0) && t.set(2, 2, 1.0) && !t.set(3, 1, 1.0), "pheromone: cap kept");
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

void TestPheromoneRows(Checker& c) {
    PheromoneTable t(5);
    t.set(9, 3, 0.5);
    t.set(9, -2, 0.25);
    t.set(9, 7, 0.5);
    t.set(4, 1, 1.0);

    // row(): a view in ascending neighbour order, empty for an unknown dest.
    const auto r = t.row(9);
    c.check(r.size() == 3 && r[0] == PheromoneTable::Entry{-2, 0.25} &&
                r[1] == PheromoneTable::Entry{3, 0.5} && r[2] == PheromoneTable::Entry{7, 0.5},
            "row ascending");
    c.check(t.row(5).empty(), "no row for an unknown destination");
    const auto e = t.entries(9);
    c.check(std::equal(e.begin(), e.end(), r.begin(), r.end()), "entries() is a copy of row()");
    // Updating an existing entry keeps the view valid and shows through it.
    const auto* first = t.row(9).data();
    c.check(t.set(9, 3, 0.75) && t.add(9, 7, 0.125) && t.row(9).data() == first &&
                t.row(9)[1].value == 0.75 && t.row(9)[2].value == 0.625,
            "set/add on an entry keep the row in place");

    // forEach(): the same entries, the same order.
    std::vector<PheromoneTable::Entry> seen;
    t.forEach(9, [&seen](const PheromoneTable::Entry& x) { seen.push_back(x); });
    c.check(seen == t.entries(9), "forEach visits the row in order");
    int calls = 0;
    t.forEach(5, [&calls](const PheromoneTable::Entry&) { ++calls; });
    c.check(calls == 0, "forEach on an unknown destination calls nothing");

    // best / bestEntry in place, with and without an excluded neighbour.
    c.check(t.best(9) == 0.75 && t.best(9, 3) == 0.625 && t.best(9, 99) == 0.75,
            "best with an exclude");
    c.check(t.bestEntry(9) == PheromoneTable::Entry{3, 0.75}, "bestEntry");
    c.check(t.bestEntry(9, 3) == PheromoneTable::Entry{7, 0.625}, "bestEntry excluding the best");
    t.set(9, 7, 0.75);
    c.check(t.bestEntry(9).neighbor == 3, "a tie goes to the lowest neighbour");
    c.check(t.bestEntry(5) == PheromoneTable::Entry{} && t.bestEntry(5).neighbor == kInvalidAddress,
            "no row: no best");
    c.check(t.bestEntry(4, 1).neighbor == kInvalidAddress && t.best(4, 1) == 0.0,
            "only the excluded neighbour: no best");
    PheromoneTable z(4);
    z.set(1, 1, 0.0);
    z.set(1, 2, -1.0);
    c.check(z.bestEntry(1).neighbor == kInvalidAddress && z.best(1) == 0.0,
            "no positive value: no best");

    // add(): one lookup, the same results as get() + set().
    c.check(t.add(4, 2, 0.5) && t.full() && t.get(4, 2) == 0.5, "add creates up to the cap");
    c.check(!t.add(4, 3, 0.5) && !t.contains(4, 3), "add of a new entry past the cap is refused");
    c.check(!t.add(8, 1, 0.5) && t.row(8).empty(), "a new row past the cap is refused");
    c.check(t.add(4, 2, 0.25) && t.get(4, 2) == 0.75, "add to an entry at the cap works");
    c.check(!t.add(4, 2, std::numeric_limits<double>::infinity()) && t.get(4, 2) == 0.75,
            "add refuses a non-finite result and keeps the value");
    PheromoneTable s(2);
    c.check(s.add(1, 1, -0.0) && !std::signbit(s.get(1, 1)),
            "a new entry is 0.0 + delta, as get() + delta was");
    c.check(!s.add(1, 2, std::nan("")) && !s.contains(1, 2), "add refuses NaN for a new entry");

    // Removing the last entry of a row removes the row.
    PheromoneTable d(4);
    d.set(1, 1, 1.0);
    d.set(2, 1, 1.0);
    d.set(2, 2, 1.0);
    c.check(d.erase(1, 1) && d.destinations() == std::vector<NodeAddress>{2} && d.size() == 2,
            "erase drops an emptied row");
    c.check(d.eraseNeighbor(1) == 1 && d.row(2).size() == 1 && d.size() == 1, "eraseNeighbor");
    c.check(d.eraseDestination(2) == 1 && d.empty() && d.destinations().empty(),
            "eraseDestination");
}

void TestOnePassEvaporation(Checker& c) {
    PheromoneTable t(8);
    t.set(1, 1, 0.5);   // * 0.5 = 0.25: exactly the floor, stays
    t.set(1, 2, 0.4);   // 0.2: below, goes
    t.set(1, 3, 1.0);   // 0.5: stays
    t.set(2, 5, 0.25);  // 0.125: goes, and its row with it
    t.set(3, 1, 0.3);   // 0.15: goes
    t.set(3, 4, 0.75);  // 0.375: stays
    c.check(t.evaporate(0.5, 0.25) == 3, "three entries fall below the floor");
    c.check(t.size() == 3 && !t.full(), "size follows, the freed room is free");
    c.check(t.get(1, 1) == 0.25 && t.contains(1, 1), "an entry at exactly the floor stays");
    c.check(!t.contains(1, 2) && !t.contains(3, 1), "entries below the floor go");
    c.check(t.destinations() == std::vector<NodeAddress>{1, 3}, "an emptied row goes");
    c.check(t.row(1).size() == 2 && t.row(1)[0].neighbor == 1 && t.row(1)[1].neighbor == 3 &&
                t.row(3).size() == 1 && t.row(3)[0] == PheromoneTable::Entry{4, 0.375},
            "the survivors keep their order and their aged values");
    c.check(t.evaporate(0.5, std::nan("")) == 0 && t.size() == 3 && t.get(1, 3) == 0.25,
            "a NaN floor ages and removes nothing");
    c.check(t.evaporate(1.0, 1.0) == 3 && t.empty() && t.destinations().empty(),
            "a floor above every value empties the table");
}

namespace {

/// The table as it was stored before #45: a std::map on (destination,
/// neighbour), with the same operations written the old way. The flat rows
/// must agree with it after every operation.
class MapModel {
public:
    explicit MapModel(std::size_t capacity) : capacity_(capacity) {}
    bool set(NodeAddress d, NodeAddress n, double v) {
        if (!std::isfinite(v)) return false;
        auto it = m_.find({d, n});
        if (it != m_.end()) {
            it->second = v;
            return true;
        }
        if (m_.size() >= capacity_) return false;
        m_.emplace(std::make_pair(d, n), v);
        return true;
    }
    bool add(NodeAddress d, NodeAddress n, double delta) { return set(d, n, get(d, n) + delta); }
    double get(NodeAddress d, NodeAddress n) const {
        auto it = m_.find({d, n});
        return it != m_.end() ? it->second : 0.0;
    }
    bool erase(NodeAddress d, NodeAddress n) { return m_.erase({d, n}) > 0; }
    std::size_t eraseNeighbor(NodeAddress n) {
        return std::erase_if(m_, [n](const auto& kv) { return kv.first.second == n; });
    }
    std::size_t eraseDestination(NodeAddress d) {
        return std::erase_if(m_, [d](const auto& kv) { return kv.first.first == d; });
    }
    std::size_t evaporate(double retain, double floor) {
        if (!(retain >= 0.0 && retain <= 1.0)) return 0;
        for (auto& kv : m_)
            kv.second *= retain;
        return std::erase_if(m_, [floor](const auto& kv) { return kv.second < floor; });
    }
    std::vector<std::pair<std::pair<NodeAddress, NodeAddress>, double>> all() const {
        return {m_.begin(), m_.end()};
    }

private:
    std::size_t capacity_;
    std::map<std::pair<NodeAddress, NodeAddress>, double> m_;
};

std::vector<std::pair<std::pair<NodeAddress, NodeAddress>, double>>
flatten(const PheromoneTable& t) {
    std::vector<std::pair<std::pair<NodeAddress, NodeAddress>, double>> out;
    for (NodeAddress d : t.destinations()) {
        for (const auto& e : t.row(d))
            out.push_back({{d, e.neighbor}, e.value});
    }
    return out;
}

}  // namespace

void TestPheromoneTableModel(Checker& c) {
    // A seeded random sequence of every mutating operation, on a small
    // address space so rows fill, empty and refill: the flat rows hold the
    // same entries, in the same order, with bit-identical values, and return
    // the same results as the std::map storage they replace (class A, #45).
    SeededRng rng(45);
    PheromoneTable t(24);
    MapModel m(24);
    bool same = true;
    for (int i = 0; i < 20000 && same; ++i) {
        const NodeAddress d = rng.uniformInt(9) - 2;
        const NodeAddress n = rng.uniformInt(9) - 2;
        const double v = rng.uniform() * 2.0 - 0.25;
        switch (rng.uniformInt(8)) {
        case 0:
        case 1:
            same = t.set(d, n, v) == m.set(d, n, v);
            break;
        case 2:
        case 3:
            same = t.add(d, n, v) == m.add(d, n, v);
            break;
        case 4:
            same = t.erase(d, n) == m.erase(d, n);
            break;
        case 5:
            same = rng.uniformInt(4) == 0 ? t.eraseNeighbor(n) == m.eraseNeighbor(n)
                                          : t.eraseDestination(d) == m.eraseDestination(d);
            break;
        default: {
            const double retain = rng.uniform();
            const double floor = rng.uniform() * 0.1;
            same = t.evaporate(retain, floor) == m.evaporate(retain, floor);
        }
        }
        same = same && flatten(t) == m.all() && t.size() == m.all().size() &&
               t.get(d, n) == m.get(d, n);
    }
    c.check(same, "flat rows match the std::map storage, operation by operation");
}

}  // namespace swarm::core::test
