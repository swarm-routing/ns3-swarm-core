// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Fundamental types of the simulator-free primitives. Derived from
// swarm-routing/AntHocNet core/include/anthocnet/core/types.h @ 78c4640
// (MIT OR GPL-2.0-only).
//
// Nothing in core/ includes a simulator header (golden rule 10): an address is
// a plain integer, so an adapter passes its own address type straight through.
#ifndef SWARM_CORE_TYPES_H
#define SWARM_CORE_TYPES_H

#include <cstdint>

namespace swarm::core {

/// Network-layer node address: the width of ns-3's 32-bit IPv4 address payload.
using NodeAddress = std::int32_t;

/// Simulation time, in seconds.
using Time = double;

/// "No such node / no route" sentinel.
inline constexpr NodeAddress kInvalidAddress = -1;

}  // namespace swarm::core

#endif  // SWARM_CORE_TYPES_H
