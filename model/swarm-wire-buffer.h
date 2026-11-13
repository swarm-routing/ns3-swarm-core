// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// The core/ wire format (core/swarm-wire.h) straight into and out of an ns-3
// Buffer (#46): a protocol's ns3::Header serializes its core's fields without
// staging them through a heap vector, and deserializes into a stack buffer.
//
//   void MyHeader::Serialize(Buffer::Iterator start) const {
//       BufferWireWriter w(start);
//       core::encode(m_msg, w);            // the codec, templated on the writer
//   }
//   uint32_t MyHeader::Deserialize(Buffer::Iterator start) {
//       std::array<uint8_t, kMaxSize> scratch;
//       const auto bytes = ReadWireBytes(start, scratch, GetSerializedSize());
//       swarm::core::WireReader r(bytes);  // fails closed on a short read
//       ...
//   }
#ifndef SWARM_WIRE_BUFFER_H
#define SWARM_WIRE_BUFFER_H

#include "ns3/buffer.h"
#include "ns3/swarm-wire.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ns3 {

/// A swarm::core wire writer whose bytes go straight to a Buffer::Iterator
/// (which advances). The caller sized the buffer (GetSerializedSize), as for
/// any ns3::Header.
class BufferWireWriter : public swarm::core::WireFields<BufferWireWriter> {
public:
    explicit BufferWireWriter(Buffer::Iterator& it) : m_it(it) {}

    void append(const uint8_t* data, std::size_t n) {
        m_it.Write(data, static_cast<uint32_t>(n));
        m_size += n;
    }
    /// Bytes written through this writer.
    std::size_t size() const { return m_size; }

private:
    Buffer::Iterator& m_it;
    std::size_t m_size = 0;
};

/// Reads up to `n` bytes from `it` (which advances) into `scratch` and
/// returns the bytes read: fewer than `n` when the buffer or `scratch` is
/// shorter, so a WireReader over the result fails closed instead of reading
/// past either.
inline std::span<const uint8_t> ReadWireBytes(Buffer::Iterator& it, std::span<uint8_t> scratch,
                                              uint32_t n) {
    const uint32_t take =
        std::min({n, it.GetRemainingSize(),
                  static_cast<uint32_t>(std::min<std::size_t>(scratch.size(), UINT32_MAX))});
    it.Read(scratch.data(), take);
    return scratch.first(take);
}

}  // namespace ns3

#endif  // SWARM_WIRE_BUFFER_H
