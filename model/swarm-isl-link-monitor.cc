// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Derived from swarm-routing/AntHocNet ns3/anthocnet/model/anthocnet-routing-protocol.cc
// @ 3e53f04 (GPL-2.0-only); see swarm-isl-link-monitor.h.
#include "swarm-isl-link-monitor.h"

#include "swarm-ns3-ports.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/queue.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("IslLinkMonitor");
NS_OBJECT_ENSURE_REGISTERED(IslLinkMonitor);

TypeId IslLinkMonitor::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::IslLinkMonitor")
            .SetParent<Object>()
            .SetGroupName("SwarmCore")
            .AddConstructor<IslLinkMonitor>()
            .AddAttribute("ServiceTimeAlpha",
                          "Weight of the previous estimate in the service-time EWMA "
                          "(0.9, AntHocNet's MacServiceAlpha).",
                          DoubleValue(0.9), MakeDoubleAccessor(&IslLinkMonitor::m_alpha),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("TrustAfterFailure",
                          "Whether a link that has gone down once still gives its "
                          "interface's neighbour set from link state alone (false: "
                          "hello comes back on it, the ISL seam gate of #4).",
                          BooleanValue(false),
                          MakeBooleanAccessor(&IslLinkMonitor::m_trustAfterFailure),
                          MakeBooleanChecker())
            .AddTraceSource("LinkChange", "A point-to-point link went up or down (peer IP, up).",
                            MakeTraceSourceAccessor(&IslLinkMonitor::m_linkChangeTrace),
                            "ns3::IslLinkMonitor::LinkChangeCallback")
            .AddTraceSource("PeerLinkChange",
                            "The far end of a point-to-point link took its interface down or "
                            "up (peer IP, up): the carrier this end sees (#4).",
                            MakeTraceSourceAccessor(&IslLinkMonitor::m_peerLinkChangeTrace),
                            "ns3::IslLinkMonitor::LinkChangeCallback");
    return tid;
}

IslLinkMonitor::IslLinkMonitor() : m_alpha(0.9), m_trustAfterFailure(false) {}

void IslLinkMonitor::DoDispose() {
    m_node = nullptr;
    m_links.clear();
    Object::DoDispose();
}

void IslLinkMonitor::Install(Ptr<Node> node) {
    NS_ASSERT_MSG(!m_node, "IslLinkMonitor::Install called twice");
    m_node = node;
    for (uint32_t i = 0; i < node->GetNDevices(); ++i) {
        Ptr<PointToPointNetDevice> dev = node->GetDevice(i)->GetObject<PointToPointNetDevice>();
        if (!dev) {
            continue;
        }
        Link link;
        link.device = dev;
        m_links.push_back(link);
        if (dev->GetQueue()) {
            dev->GetQueue()->TraceConnectWithoutContext(
                "Dequeue",
                MakeBoundCallback(&IslLinkMonitor::DequeueThunk, this, m_links.size() - 1));
        }
    }
    node->AggregateObject(this);
}

void IslLinkMonitor::DequeueThunk(IslLinkMonitor* self, std::size_t link, Ptr<const Packet>) {
    self->NotifyDequeue(link);
}

void IslLinkMonitor::NotifyDequeue(std::size_t link) {
    // A pure service sample is the spacing between two dequeues with the queue
    // never empty in between: the device pulls the next packet the moment the
    // previous one is serialised, so the spacing IS the service time, and
    // never includes propagation (AntHocNet #206).
    Link& l = m_links[link];
    const Time now = Simulator::Now();
    if (l.backlogAtLastDequeue && l.lastDequeue.IsStrictlyPositive()) {
        const double sample = (now - l.lastDequeue).GetSeconds();
        if (sample > 0.0) {
            l.ewmaS = l.ewmaS > 0.0 ? m_alpha * l.ewmaS + (1.0 - m_alpha) * sample : sample;
        }
    }
    l.lastDequeue = now;
    l.backlogAtLastDequeue = l.device->GetQueue()->GetNPackets() > 0;
}

int32_t IslLinkMonitor::InterfaceOf(const Link& link) const {
    Ptr<Ipv4> ipv4 = m_node ? m_node->GetObject<Ipv4>() : nullptr;
    return ipv4 ? ipv4->GetInterfaceForDevice(link.device) : -1;
}

Ipv4Address IslLinkMonitor::PeerOf(const Link& link) const {
    Ptr<Channel> channel = link.device->GetChannel();
    if (!channel || channel->GetNDevices() != 2) {
        return Ipv4Address();
    }
    // The other end of the wire: a point-to-point link has exactly one peer,
    // which its terminal knows by construction.
    Ptr<NetDevice> peer =
        channel->GetDevice(0) == link.device ? channel->GetDevice(1) : channel->GetDevice(0);
    Ptr<Ipv4> ipv4 = peer->GetNode()->GetObject<Ipv4>();
    if (!ipv4) {
        return Ipv4Address();
    }
    const int32_t iface = ipv4->GetInterfaceForDevice(peer);
    if (iface < 0 || ipv4->GetNAddresses(iface) == 0) {
        return Ipv4Address();
    }
    return ipv4->GetAddress(iface, 0).GetLocal();
}

std::vector<swarm::core::NodeAddress> IslLinkMonitor::neighbors() const {
    std::vector<swarm::core::NodeAddress> out;
    Ptr<Ipv4> ipv4 = m_node ? m_node->GetObject<Ipv4>() : nullptr;
    for (const Link& l : m_links) {
        const int32_t iface = InterfaceOf(l);
        if (!l.device->IsLinkUp() || iface < 0 || !ipv4->IsUp(iface)) {
            continue;
        }
        const Ipv4Address peer = PeerOf(l);
        if (peer != Ipv4Address()) {
            out.push_back(ToNodeAddress(peer));
        }
    }
    return out;
}

const IslLinkMonitor::Link* IslLinkMonitor::LinkTowards(swarm::core::NodeAddress nextHop) const {
    if (nextHop == swarm::core::kInvalidAddress) {
        return nullptr;
    }
    for (const Link& l : m_links) {
        if (ToNodeAddress(PeerOf(l)) == nextHop) {
            return &l;
        }
    }
    return nullptr;
}

int IslLinkMonitor::macQueueLength(swarm::core::NodeAddress nextHop) const {
    if (const Link* l = LinkTowards(nextHop)) {
        return l->device->GetQueue() ? static_cast<int>(l->device->GetQueue()->GetNPackets()) : 0;
    }
    uint32_t total = 0;
    for (const Link& l : m_links) {
        total += l.device->GetQueue() ? l.device->GetQueue()->GetNPackets() : 0;
    }
    return static_cast<int>(total);
}

swarm::core::Time IslLinkMonitor::macServiceTime(swarm::core::NodeAddress nextHop) const {
    if (const Link* l = LinkTowards(nextHop)) {
        return l->ewmaS;
    }
    double sum = 0.0;
    int n = 0;
    for (const Link& l : m_links) {
        if (l.ewmaS > 0.0) {
            sum += l.ewmaS;
            ++n;
        }
    }
    return n > 0 ? sum / n : 0.0;
}

void IslLinkMonitor::NotifyInterface(uint32_t interface, bool up) {
    for (Link& l : m_links) {
        if (InterfaceOf(l) == static_cast<int32_t>(interface)) {
            l.failed |= !up;
            const Ipv4Address peer = PeerOf(l);
            if (peer != Ipv4Address()) {
                m_linkChangeTrace(peer, up);
            }
            Ptr<Channel> channel = l.device->GetChannel();
            if (channel && channel->GetNDevices() == 2) {
                Ptr<NetDevice> far = channel->GetDevice(0) == l.device ? channel->GetDevice(1)
                                                                       : channel->GetDevice(0);
                if (Ptr<IslLinkMonitor> m = far->GetNode()->GetObject<IslLinkMonitor>()) {
                    m->FarEndChanged(far, up);
                }
            }
        }
    }
}

void IslLinkMonitor::FarEndChanged(Ptr<NetDevice> device, bool up) {
    for (Link& l : m_links) {
        if (l.device == device) {
            l.failed |= !up;
            const Ipv4Address peer = PeerOf(l);
            if (peer != Ipv4Address()) {
                m_peerLinkChangeTrace(peer, up);
            }
        }
    }
}

const IslLinkMonitor::Link* IslLinkMonitor::LinkOn(uint32_t interface) const {
    for (const Link& l : m_links) {
        if (InterfaceOf(l) == static_cast<int32_t>(interface)) {
            return &l;
        }
    }
    return nullptr;
}

Ipv4Address IslLinkMonitor::PeerOnInterface(uint32_t interface) const {
    const Link* l = LinkOn(interface);
    return l ? PeerOf(*l) : Ipv4Address();
}

bool IslLinkMonitor::IsAuthoritative(uint32_t interface) const {
    const Link* l = LinkOn(interface);
    return l && PeerOf(*l) != Ipv4Address() && (!l->failed || m_trustAfterFailure);
}

}  // namespace ns3
