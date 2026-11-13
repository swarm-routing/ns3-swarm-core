// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// DeferredPacketQueue: data packets held while a protocol looks for a route,
// with the forwarding callbacks to resume them. Bounded by size and by age
// (org rule 7), both attributes. Derived from swarm-routing/AntHocNet
// ns3/anthocnet/model/anthocnet-rqueue.{h,cc} and the hold queue of
// ns3/baselines/adaptive/model/adaptive-routing-base.{h,cc} @ 3e53f04
// (GPL-2.0-only): drop-oldest on overflow through the error callback (#215
// there), expiry measured from the first enqueue, hold-time accounting (#21,
// #805 there). AntHocNet's per-reason hold caps stay behind (golden rule 11).
#ifndef SWARM_DEFERRED_PACKET_QUEUE_H
#define SWARM_DEFERRED_PACKET_QUEUE_H

#include "ns3/ipv4-header.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace ns3 {

/// Why a packet left the queue without being forwarded.
enum class DeferredDropReason : uint8_t {
    Overflow = 0,   ///< the queue was full: the oldest packet made room (MaxPackets)
    Expired = 1,    ///< it waited longer than MaxAge for a route
    Discarded = 2,  ///< the protocol gave up on its destination (DiscardPending)
    NodeDown = 3,   ///< the node shut down or crashed (Flush, #43)
};

/// What the queue has done over a run. A probe reads it at the end; the
/// counts add up: enqueued == released + overflow + expired + discarded +
/// nodeDown + Size().
struct DeferredQueueStats {
    uint64_t enqueued = 0;
    uint64_t released = 0;
    uint64_t overflow = 0;
    uint64_t expired = 0;
    uint64_t discarded = 0;
    uint64_t nodeDown = 0;          ///< dropped by Flush: the node went down (#43)
    double releasedHoldSumS = 0.0;  ///< total wait of the released packets
    double releasedHoldMaxS = 0.0;  ///< longest wait of a released packet
};

/// One held packet and the callbacks that resume it, or report its loss.
struct DeferredPacket {
    Ptr<const Packet> packet;
    Ipv4Header header;
    Ipv4RoutingProtocol::UnicastForwardCallback ucb;
    Ipv4RoutingProtocol::ErrorCallback ecb;
    Time enqueued;  ///< when it first entered the queue
};

class DeferredPacketQueue : public Object {
public:
    static TypeId GetTypeId();
    DeferredPacketQueue();

    /// Holds a packet. A full queue first drops its oldest packet through that
    /// packet's error callback (counted as Overflow). A packet that already
    /// carries an enqueue time (it was released and came back) keeps it, so
    /// re-queuing never extends its life past MaxAge.
    void Enqueue(DeferredPacket entry);

    /// Puts back packets Release returned and the protocol still has no next
    /// hop for, oldest first, each as Enqueue would (counted as enqueued, a
    /// full queue drops its oldest), with one purge for the whole batch
    /// (#46): draining a queue is linear, not quadratic.
    void Requeue(std::vector<DeferredPacket> entries);

    /// Removes and returns every packet to `dst` (counted as released, with
    /// their hold time), oldest first. Expired packets are dropped first.
    std::vector<DeferredPacket> Release(Ipv4Address dst);

    /// Drops every packet to `dst` through its error callback (Discarded).
    void Discard(Ipv4Address dst);

    /// Drops every packet older than MaxAge through its error callback.
    void Purge();

    /// Drops every packet through its error callback (NodeDown), oldest
    /// first: the node shut down or crashed (#43).
    void Flush();

    /// Packets to `dst` waiting (expired ones excluded).
    bool Has(Ipv4Address dst) const;
    /// Packets waiting, including any not yet purged.
    uint32_t Size() const { return static_cast<uint32_t>(m_queue.size()); }
    /// Distinct destinations with a packet waiting, in first-enqueue order.
    std::vector<Ipv4Address> Destinations() const;

    uint32_t GetMaxPackets() const { return m_maxPackets; }
    Time GetMaxAge() const { return m_maxAge; }
    const DeferredQueueStats& Stats() const { return m_stats; }

    /// Fired for every packet dropped from the queue.
    typedef void (*DropCallback)(Ptr<const Packet> packet, const Ipv4Header& header,
                                 DeferredDropReason reason);

protected:
    void DoDispose() override;

private:
    void Drop(const DeferredPacket& e, DeferredDropReason reason);
    /// Enqueue without the purge.
    void Admit(DeferredPacket entry);
    /// Removes and returns the entries matching `pred`, in order; the rest
    /// keep theirs. Moves, never copies; returns at once when none match.
    template <typename Pred> std::vector<DeferredPacket> Extract(Pred pred);
    bool Expired(const DeferredPacket& e) const;

    std::deque<DeferredPacket> m_queue;
    uint32_t m_maxPackets;
    Time m_maxAge;
    /// No held packet was enqueued before this (#46): lets Purge skip the scan.
    Time m_oldest;
    DeferredQueueStats m_stats;
    TracedCallback<Ptr<const Packet>, const Ipv4Header&, DeferredDropReason> m_dropTrace;
};

}  // namespace ns3

#endif  // SWARM_DEFERRED_PACKET_QUEUE_H
