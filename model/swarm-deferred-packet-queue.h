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
// #805 there). Hold classes (#126), from the same anthocnet-rqueue.{h,cc} @
// 7c3aaf8: a protocol-defined class per packet (AntHocNet's hold reason) with
// its own cap and statistics, and expiry re-armed at each enqueue; which class
// a packet is in stays the protocol's choice (golden rule 11).
#ifndef SWARM_DEFERRED_PACKET_QUEUE_H
#define SWARM_DEFERRED_PACKET_QUEUE_H

#include "ns3/ipv4-header.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"

#include <array>
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

/// How many hold classes the queue keeps a cap and statistics for (#126). A
/// packet's class is protocol-defined (AntHocNet: first setup, reconvergence,
/// repair); 0 is the default. A class at or above this counts as class 0.
inline constexpr uint8_t kDeferredHoldClasses = 8;

/// One hold class's account (#126), AntHocNet's per-reason HoldStats.
/// `forwarded*`: packets that left the queue toward a next hop
/// (NoteForwarded, which SwarmRoutingBase::ReleaseDeferred calls), with the
/// time since their first enqueue. `dropped*`: packets that left without a
/// route arriving in time, by Overflow or Expired. Discarded and NodeDown
/// drops are counted only in the totals (DeferredQueueStats).
struct DeferredClassStats {
    uint64_t forwarded = 0;
    double forwardedHoldSumS = 0.0;
    double forwardedHoldMaxS = 0.0;
    uint64_t dropped = 0;
    double droppedHoldSumS = 0.0;
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
    /// Per hold class (#126); the totals above count every class.
    std::array<DeferredClassStats, kDeferredHoldClasses> byClass{};
};

/// One held packet and the callbacks that resume it, or report its loss.
struct DeferredPacket {
    Ptr<const Packet> packet;
    Ipv4Header header;
    Ipv4RoutingProtocol::UnicastForwardCallback ucb;
    Ipv4RoutingProtocol::ErrorCallback ecb;
    Time enqueued;  ///< when it first entered the queue
    /// Protocol-defined hold class (#126): selects the class cap and the
    /// per-class statistics. 0 by default.
    uint8_t holdClass = 0;
    /// When it last entered the queue (Enqueue or Requeue); set by the queue.
    Time latest;
};

class DeferredPacketQueue : public Object {
public:
    static TypeId GetTypeId();
    DeferredPacketQueue();

    /// Holds a packet. A full queue first drops its oldest packet through that
    /// packet's error callback (counted as Overflow). A packet that already
    /// carries an enqueue time (it was released and came back) keeps it as
    /// its first enqueue; `latest` becomes now. A packet that arrives already
    /// expired is dropped at once (Expired).
    ///
    /// When it expires: MaxAge after its first enqueue (the default), or,
    /// with RearmOnEnqueue, MaxAge after its latest enqueue; in both modes no
    /// later than its class cap after its first enqueue, when that class has
    /// a cap (SetHoldClassCap). So by default re-queuing never extends a
    /// packet's life past MaxAge, and with a class cap never past the cap.
    void Enqueue(DeferredPacket entry);

    /// Puts back packets Release returned and the protocol still has no next
    /// hop for, oldest first, each as Enqueue would (counted as enqueued,
    /// keeping its first enqueue time and hold class, a full queue drops its
    /// oldest), with one purge for the whole batch (#46): draining a queue is
    /// linear, not quadratic.
    void Requeue(std::vector<DeferredPacket> entries);

    /// Accounts a packet Release returned as forwarded: its class's
    /// `forwarded` statistics, with the time since its first enqueue (#126).
    /// SwarmRoutingBase::ReleaseDeferred calls it for every packet it sends;
    /// a protocol that releases packets itself calls it before sending one.
    void NoteForwarded(const DeferredPacket& e);

    /// The cap of hold class `holdClass` (#126): a packet of that class
    /// expires at the latest `cap` after its first enqueue, whatever MaxAge
    /// and RearmOnEnqueue say. Zero (the default) means no cap. AntHocNet's
    /// ReconvHoldCap and RepairHoldCap. A class at or above
    /// kDeferredHoldClasses sets class 0's.
    void SetHoldClassCap(uint8_t holdClass, Time cap);
    Time GetHoldClassCap(uint8_t holdClass) const;

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
    bool GetRearmOnEnqueue() const { return m_rearm; }
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
    /// The shortest time a held packet can wait before it expires: MaxAge,
    /// or a shorter class cap.
    Time MinWindow() const;
    static uint8_t ClassOf(const DeferredPacket& e) {
        return e.holdClass < kDeferredHoldClasses ? e.holdClass : 0;
    }

    std::deque<DeferredPacket> m_queue;
    uint32_t m_maxPackets;
    Time m_maxAge;
    bool m_rearm;                                        ///< RearmOnEnqueue
    std::array<Time, kDeferredHoldClasses> m_holdCap{};  ///< 0: no cap
    /// No held packet was first enqueued before this (#46): lets Purge skip
    /// the scan.
    Time m_oldest;
    DeferredQueueStats m_stats;
    TracedCallback<Ptr<const Packet>, const Ipv4Header&, DeferredDropReason> m_dropTrace;
};

}  // namespace ns3

#endif  // SWARM_DEFERRED_PACKET_QUEUE_H
