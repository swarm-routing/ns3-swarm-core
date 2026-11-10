// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The wire helpers (org rule 3): deterministic roundtrip of every field, the
// fail-closed reader, and a seeded fuzz of a sample frame that uses every
// reader. The fuzz checks the strongest property a canonical format has: a
// buffer either fails to decode, or decodes and re-encodes to the very same
// bytes. Under the sanitized CI leg an out-of-bounds read would abort.
#include "../swarm-visited-path.h"
#include "../swarm-wire.h"
#include "swarm-core-tests.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace swarm::core::test {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t kSampleVersion = 0x2A;
constexpr std::size_t kSamplePathCap = 8;

/// A frame that uses every writer and reader: the fuzz target.
struct Sample {
    std::uint8_t a = 0;
    bool flag = false;
    std::uint16_t b = 0;
    std::uint32_t c = 0;
    std::uint64_t d = 0;
    std::int32_t e = 0;
    float f = 0.0f;
    double g = 0.0;
    VisitedPath path{kSamplePathCap};
};

Bytes encode(const Sample& s) {
    Bytes out;
    WireWriter w(out);
    w.version(kSampleVersion);
    w.u8(s.a);
    w.boolean(s.flag);
    w.u16(s.b);
    w.u32(s.c);
    w.u64(s.d);
    w.i32(s.e);
    w.f32(s.f);
    w.f64(s.g);
    s.path.write(w);
    return out;
}

bool decode(const Bytes& in, Sample& s) {
    WireReader r(in);
    r.expectVersion(kSampleVersion);
    s.a = r.u8();
    s.flag = r.boolean();
    s.b = r.u16();
    s.c = r.u32();
    s.d = r.u64();
    s.e = r.i32();
    s.f = r.f32();
    s.g = r.f64();
    VisitedPath::read(r, s.path);
    return r.atEnd();
}

Sample randomSample(SeededRng& rng) {
    Sample s;
    s.a = static_cast<std::uint8_t>(rng.next());
    s.flag = (rng.next() & 1u) != 0;
    s.b = static_cast<std::uint16_t>(rng.next());
    s.c = static_cast<std::uint32_t>(rng.next());
    s.d = rng.next();
    s.e = static_cast<std::int32_t>(static_cast<std::uint32_t>(rng.next()));
    s.f = std::bit_cast<float>(static_cast<std::uint32_t>(rng.next()));
    s.g = std::bit_cast<double>(rng.next());
    const auto hops = rng.next() % (kSamplePathCap + 1);
    for (std::uint64_t i = 0; i < hops; ++i) {
        s.path.push(static_cast<NodeAddress>(rng.next()), std::bit_cast<double>(rng.next()));
    }
    return s;
}

/// The fuzz property for one buffer: fails, or round-trips to itself.
bool canonical(const Bytes& in) {
    Sample s;
    if (!decode(in, s)) return true;
    return encode(s) == in;
}

}  // namespace

void TestWireRoundtrip(Checker& c) {
    Bytes out;
    WireWriter w(out);
    w.version(7);
    w.u8(0xAB);
    w.boolean(true);
    w.boolean(false);
    w.u16(0x1234);
    w.u32(0xDEADBEEFu);
    w.u64(0x0102030405060708ull);
    w.i32(-2);
    w.f32(1.5f);
    w.f64(-0.0);
    w.f64(std::numeric_limits<double>::infinity());
    c.check(w.size() == 1 + 1 + 1 + 1 + 2 + 4 + 8 + 4 + 4 + 8 + 8, "writer size");

    // Little-endian whatever the host: spot-check the bytes.
    c.check(out[4] == 0x34 && out[5] == 0x12, "u16 is little-endian");
    c.check(out[6] == 0xEF && out[9] == 0xDE, "u32 is little-endian");
    c.check(out[10] == 0x08 && out[17] == 0x01, "u64 is little-endian");
    c.check(out[18] == 0xFE && out[21] == 0xFF, "i32 is two's complement");

    WireReader r(out);
    c.check(r.expectVersion(7), "version");
    c.check(r.u8() == 0xAB, "u8");
    c.check(r.boolean(), "bool true");
    c.check(!r.boolean() && r.ok(), "bool false");
    c.check(r.u16() == 0x1234, "u16");
    c.check(r.u32() == 0xDEADBEEFu, "u32");
    c.check(r.u64() == 0x0102030405060708ull, "u64");
    c.check(r.i32() == -2, "i32");
    c.check(r.f32() == 1.5f, "f32");
    const double negZero = r.f64();
    c.check(negZero == 0.0 && std::signbit(negZero), "f64 keeps -0.0");
    c.check(std::isinf(r.f64()), "f64 keeps infinity");
    c.check(r.atEnd(), "every byte consumed");

    // A NaN payload survives bit for bit.
    const auto nanBits = std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN());
    Bytes nan;
    WireWriter wn(nan);
    wn.f64(std::bit_cast<double>(nanBits | 1u));
    WireReader rn(nan);
    c.check(std::bit_cast<std::uint64_t>(rn.f64()) == (nanBits | 1u), "NaN payload kept");

    // Writing appends: a second writer continues an existing buffer.
    Bytes two{0x01};
    WireWriter w2(two);
    w2.u16(2);
    c.check(two == Bytes{0x01, 0x02, 0x00}, "writer appends");

    // Seeded random samples round-trip exactly.
    SeededRng rng(0x5EED);
    for (int i = 0; i < 200; ++i) {
        const Sample s = randomSample(rng);
        const Bytes bytes = encode(s);
        Sample back;
        const bool ok = decode(bytes, back);
        c.check(ok && encode(back) == bytes, "random sample round-trips");
        c.check(ok && back.path.size() == s.path.size() &&
                    (s.path.empty() || back.path.back().node == s.path.back().node),
                "random path round-trips");
        if (!ok) break;
    }
}

void TestWireFailsClosed(Checker& c) {
    const Bytes empty;
    WireReader r0(empty);
    c.check(r0.ok() && r0.atEnd(), "empty reader starts ok and at end");
    c.check(r0.u32() == 0 && !r0.ok(), "short read fails and returns 0");
    c.check(r0.u8() == 0 && !r0.ok() && !r0.atEnd(), "stays failed");

    const Bytes three{1, 2, 3};
    WireReader r1(three);
    c.check(r1.u32() == 0 && !r1.ok(), "u32 from 3 bytes fails");
    c.check(r1.remaining() == 3, "a failed read does not move");
    c.check(r1.u8() == 0, "nothing is read after a failure");

    WireReader rv(three);
    c.check(!rv.expectVersion(9) && !rv.ok(), "wrong version fails");

    const Bytes badBool{2};
    WireReader rb(badBool);
    c.check(!rb.boolean() && !rb.ok(), "bool other than 0/1 fails");

    // count(): the protocol bound and the bytes left are both enforced.
    const Bytes countFive{5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // 5 elements of 2 bytes = 10 bytes
    WireReader rc(countFive);
    c.check(rc.count(5, 2) == 5 && rc.ok(), "count within bounds");
    WireReader rcMax(countFive);
    c.check(rcMax.count(4, 2) == 0 && !rcMax.ok(), "count over the protocol bound fails");
    WireReader rcBytes(countFive);
    c.check(rcBytes.count(5, 3) == 0 && !rcBytes.ok(), "count over the bytes left fails");
    const Bytes huge{0xFF, 0xFF};
    WireReader rcHuge(huge);
    c.check(rcHuge.count(0xFFFF, 1) == 0 && !rcHuge.ok(), "a huge count cannot allocate");

    // Trailing bytes: ok() but not atEnd().
    const Bytes trailing{7, 0};
    WireReader rt(trailing);
    rt.u8();
    c.check(rt.ok() && !rt.atEnd(), "trailing bytes are not atEnd");

    WireReader rf(trailing);
    rf.fail();
    c.check(!rf.ok() && rf.u8() == 0, "fail() fails the reader");

    // Every truncation of a valid frame is rejected.
    SeededRng rng(11);
    const Bytes frame = encode(randomSample(rng));
    bool allRejected = true;
    for (std::size_t len = 0; len < frame.size(); ++len) {
        Sample s;
        if (decode(Bytes(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(len)), s)) {
            allRejected = false;
        }
    }
    c.check(allRejected, "every truncation of a frame fails");
    Bytes longer = frame;
    longer.push_back(0);
    Sample s;
    c.check(!decode(longer, s), "a trailing byte fails");
}

void TestWireFuzz(Checker& c) {
    SeededRng rng(0xF022);
    int bad = 0;

    // Random buffers, with the right version byte half the time so the fuzz
    // gets past the first check.
    for (int i = 0; i < 4000; ++i) {
        Bytes in(rng.next() % 160);
        for (auto& b : in)
            b = static_cast<std::uint8_t>(rng.next());
        if (!in.empty() && (rng.next() & 1u)) in[0] = kSampleVersion;
        if (!canonical(in)) ++bad;
    }

    // Mutations of valid frames: flip, insert, delete or overwrite bytes.
    for (int i = 0; i < 4000; ++i) {
        Bytes in = encode(randomSample(rng));
        const int edits = 1 + static_cast<int>(rng.next() % 4);
        for (int e = 0; e < edits && !in.empty(); ++e) {
            const std::size_t at = rng.next() % in.size();
            switch (rng.next() % 4) {
            case 0:
                in[at] ^= static_cast<std::uint8_t>(1u << (rng.next() % 8));
                break;
            case 1:
                in.insert(in.begin() + static_cast<std::ptrdiff_t>(at),
                          static_cast<std::uint8_t>(rng.next()));
                break;
            case 2:
                in.erase(in.begin() + static_cast<std::ptrdiff_t>(at));
                break;
            default:
                in[at] = static_cast<std::uint8_t>(rng.next());
                break;
            }
        }
        if (!canonical(in)) ++bad;
    }
    c.check(bad == 0, "every fuzzed buffer fails or round-trips to itself");
}

}  // namespace swarm::core::test
