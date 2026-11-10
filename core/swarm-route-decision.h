// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// RouteDecision: the verb a protocol's core hands back to its adapter.
// Derived from swarm-routing/AntHocNet core/include/anthocnet/core/route_decision.h
// @ 78c4640 (MIT OR GPL-2.0-only); there it carries AntHocNet's AntMessage,
// here the protocol's own message type is the template parameter.
//
// A core is pure: it never sends a packet or schedules a timer itself. It
// returns decisions, and the adapter carries them out against its simulator
// (golden rule 10; the adapter converts headers and executes decisions only).
#ifndef SWARM_CORE_ROUTE_DECISION_H
#define SWARM_CORE_ROUTE_DECISION_H

#include "swarm-types.h"

#include <optional>
#include <utility>

namespace swarm::core {

enum class RouteAction {
    None,            ///< Nothing to do.
    Unicast,         ///< Send `message` to `nextHop`.
    Broadcast,       ///< Link-layer broadcast of `message`.
    Queue,           ///< Hold the data packet until a route to `nextHop` exists.
    Deliver,         ///< Destination reached: hand it to the local transport.
    Drop,            ///< Discard (loop, TTL, no route).
    DiscardPending,  ///< Give up on `nextHop`: discard the packets queued for it.
};

/// One decision. `Message` is the protocol's control message; `message` is
/// set for Unicast and Broadcast when the protocol emits one.
template <typename Message> struct RouteDecision {
    RouteAction action = RouteAction::None;
    NodeAddress nextHop = kInvalidAddress;
    std::optional<Message> message;

    static RouteDecision none() { return {}; }
    static RouteDecision drop() { return {RouteAction::Drop, kInvalidAddress, std::nullopt}; }
    static RouteDecision deliver() { return {RouteAction::Deliver, kInvalidAddress, std::nullopt}; }
    static RouteDecision queue(NodeAddress dest) {
        return {RouteAction::Queue, dest, std::nullopt};
    }
    static RouteDecision discardPending(NodeAddress dest) {
        return {RouteAction::DiscardPending, dest, std::nullopt};
    }
    static RouteDecision unicast(NodeAddress nextHop, Message m) {
        return {RouteAction::Unicast, nextHop, std::move(m)};
    }
    static RouteDecision broadcast(Message m) {
        return {RouteAction::Broadcast, kInvalidAddress, std::move(m)};
    }
};

}  // namespace swarm::core

#endif  // SWARM_CORE_ROUTE_DECISION_H
