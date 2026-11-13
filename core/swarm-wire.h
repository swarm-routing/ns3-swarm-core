// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Little-endian wire helpers for a protocol's control headers (org rule 3).
// Derived from the Writer/Reader in swarm-routing/AntHocNet
// core/src/ant_message_codec.cpp @ 78c4640 (MIT OR GPL-2.0-only), made public
// and C++20 (std::span, std::bit_cast).
//
// The convention every swarm-routing header follows:
//   - byte 0 is the protocol's wire version (WireWriter::version,
//     WireReader::expectVersion); a changed field or unit bumps it;
//   - integers and floats are little-endian, whatever the host;
//   - a variable-length array is a u16 count, then its elements; the reader
//     checks the count against a protocol bound and against the bytes left
//     (WireReader::count) before anything is allocated.
//
// The reader fails closed: the first short read, bad version, bad bool or
// oversized count marks it failed; every later read returns 0 without moving,
// and ok() stays false. A decoder reads all its fields, then checks atEnd()
// (or ok()) once.
#ifndef SWARM_CORE_WIRE_H
#define SWARM_CORE_WIRE_H

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace swarm::core {

/// The field encoders every writer shares (#46): each field is formed in a
/// small array and handed to the writer's `append(const uint8_t*, size_t)`
/// in one call, never byte by byte.
template <typename Derived> class WireFields {
public:
    /// The protocol's wire version: by convention the first byte of a frame.
    void version(std::uint8_t v) { u8(v); }

    void u8(std::uint8_t v) { self().append(&v, 1); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void u16(std::uint16_t v) { put(v, 2); }
    void u32(std::uint32_t v) { put(v, 4); }
    void u64(std::uint64_t v) { put(v, 8); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }

private:
    Derived& self() { return static_cast<Derived&>(*this); }
    void put(std::uint64_t v, std::size_t bytes) {
        std::array<std::uint8_t, 8> b{};
        for (std::size_t i = 0; i < bytes; ++i) {
            b[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu);
        }
        self().append(b.data(), bytes);
    }
};

/// Appends little-endian fields to a byte buffer.
class WireWriter : public WireFields<WireWriter> {
public:
    /// Writes to the end of `out`; the buffer must outlive the writer.
    explicit WireWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    /// Makes room for `bytes` more, so a header of known size is written with
    /// at most one allocation (#46).
    void reserve(std::size_t bytes) { out_.reserve(out_.size() + bytes); }

    /// Bytes in the buffer (including any written before this writer).
    std::size_t size() const { return out_.size(); }

    void append(const std::uint8_t* data, std::size_t n) {
        out_.insert(out_.end(), data, data + n);
    }

private:
    std::vector<std::uint8_t>& out_;
};

/// Writes little-endian fields into a fixed buffer of `N` bytes held in the
/// writer itself: no allocation, for a header whose size is known (#46). A
/// field that does not fit is not written, and ok() turns false for good.
template <std::size_t N> class FixedWireWriter : public WireFields<FixedWireWriter<N>> {
public:
    /// False once a field did not fit; stays false.
    bool ok() const { return ok_; }
    /// Bytes written.
    std::size_t size() const { return size_; }
    std::span<const std::uint8_t> bytes() const { return {buf_.data(), size_}; }

    void append(const std::uint8_t* data, std::size_t n) {
        if (!ok_ || N - size_ < n) {
            ok_ = false;
            return;
        }
        std::memcpy(buf_.data() + size_, data, n);
        size_ += n;
    }

private:
    std::array<std::uint8_t, N> buf_{};
    std::size_t size_ = 0;
    bool ok_ = true;
};

/// Reads little-endian fields from untrusted bytes; never reads past the end.
class WireReader {
public:
    /// Reads `bytes`, which must outlive the reader.
    explicit WireReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    /// False once any read failed; stays false.
    bool ok() const { return ok_; }
    /// True when every read succeeded and every byte was consumed: the check a
    /// decoder makes last, so trailing garbage is rejected too.
    bool atEnd() const { return ok_ && pos_ == bytes_.size(); }
    /// Bytes not yet read.
    std::size_t remaining() const { return bytes_.size() - pos_; }
    /// Marks the reader failed (a decoder's own semantic check failed).
    void fail() { ok_ = false; }

    /// Reads the version byte and fails unless it is `expected`.
    bool expectVersion(std::uint8_t expected) {
        if (u8() != expected) fail();
        return ok_;
    }

    std::uint8_t u8() { return static_cast<std::uint8_t>(get(1)); }
    /// A bool is 0 or 1; any other byte fails the reader.
    bool boolean() {
        const std::uint8_t v = u8();
        if (v > 1) fail();
        return ok_ && v == 1;
    }
    std::uint16_t u16() { return static_cast<std::uint16_t>(get(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(get(4)); }
    std::uint64_t u64() { return get(8); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32() { return std::bit_cast<float>(u32()); }
    double f64() { return std::bit_cast<double>(u64()); }

    /// Reads a u16 element count and fails (returning 0) when it exceeds
    /// `maxCount` or when `count * elementSize` bytes are not left, so a
    /// decoder can size a container from it without trusting the sender.
    std::size_t count(std::uint16_t maxCount, std::size_t elementSize) {
        const std::size_t n = u16();
        if (!ok_ || n > maxCount || (elementSize != 0 && n > remaining() / elementSize)) {
            fail();
            return 0;
        }
        return n;
    }

private:
    std::uint64_t get(std::size_t n) {
        if (!ok_ || remaining() < n) {
            fail();
            return 0;
        }
        // One bounds check, then the field in one copy (#46); the bytes are
        // little-endian, so a big-endian host assembles them by shifts.
        std::uint64_t v = 0;
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(&v, bytes_.data() + pos_, n);
        } else {
            for (std::size_t i = 0; i < n; ++i) {
                v |= static_cast<std::uint64_t>(bytes_[pos_ + i]) << (8 * i);
            }
        }
        pos_ += n;
        return v;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace swarm::core

#endif  // SWARM_CORE_WIRE_H
