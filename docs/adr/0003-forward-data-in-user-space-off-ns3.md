# 0003. Forward data in user space, through an overlay device and a link-local underlay; a kernel route is an optional fast path

- **Status:** accepted (maintainer, 2026-11-16: the draft and every recommendation under "Questions decided with this ADR")
- **Date:** 2026-11-16
- **Issue:** swarm-routing/.github#255 (part of swarm-routing/.github#250, epic swarm-routing/.github#323)

It is recorded here, beside the host contract (swarm-routing/ns3-swarm-core#82),
because the hosts repository of swarm-routing/.github#253 item 2 does not exist
yet; it moves there with the host code when that repository is created.
Every per-protocol fact below was read on `origin/main` on 2026-11-16
(ns3-swarm-core `671553b`, AntHocNet `43db321`, ARA `15be777`, BeeAdHoc
`06dfc76`, Termite `de0552b`, HOPNET `dfc6e5c`) or in the issue named next to
it. A statement about Linux that was not tested is marked *to verify on a
kernel*.

## Context

On ns-3 a protocol's adapter sees every data packet and may tag it. On a Linux
computer the kernel forwards, from a table that holds one next hop per
destination. swarm-routing/AntHocNet#152 planned to write that table through
rtnetlink and named the problem itself: how a next hop drawn per packet maps
onto such a table is "the crux of this ticket", to be decided before
implementing. This ADR is that decision, for the five protocols the daemon of
swarm-routing/.github#256 hosts.

Decisions already taken, which this ADR stays inside:

- The host order, where the code lives and its licence
  (swarm-routing/.github#253, decision of 2026-11-12): a daemon after the
  standalone discrete-event host, in one hosts repository, `MIT OR
  GPL-2.0-only`, written fresh, on swarm-core 2.0.0's contract.
- Results from a host that is not ns-3 are qualitative
  (swarm-routing/.github#256, "Out of scope").
- A kernel forwarding mode is a deviation, default off, and
  swarm-routing/.github#269's test F2 measures its cost for AntHocNet before
  anyone builds it (swarm-routing/.github#255, Scope).
- No protocol's paper behaviour changes (swarm-routing/.github#250, "Out of
  scope"); bounded structures stay bounded (org rule 7); no per-network
  presets (AntHocNet ADR-0019).
- The daemon is a single-threaded event loop (epoll with timerfd) and its
  first slice runs without privileges over UDP (swarm-routing/.github#256,
  Scope).

### What each protocol needs, as the code says today

swarm-routing/.github#255's table was written on 2026-11-12. Five of its
cells no longer match `origin/main`; the table in "The completed table" below
uses the code. The differences:

| Protocol | The issue says | The code says |
|---|---|---|
| Termite | its header sits "between the IPv4 header and UDP with the IP protocol number unchanged" | "In a UDP datagram it rides right after the UDP header […]; in any other IPv4 payload it rides in front of the transport header" (Termite `ns3/model/termite-header.h`, lines 12 to 15; the adapter, `ns3/model/termite-routing-protocol.cc` lines 93, 150 and 173; swarm-routing/Termite#95) |
| BeeAdHoc | the source route is "an ns-3 `RouteTag` with no bytes on the wire" | a versioned wire trailer exists, `RouteTrailer` (BeeAdHoc `core/beeadhoc-trailer.h`), and the ns-3 module carries it "right after the UDP header […]; the trailer's bytes are on the air" (BeeAdHoc `docs/fidelity.md`, lines 28 to 31) |
| ARA | holds no packets; backtracking "needs a mark on the wire" | by default none, and a default-off switch `bufferDuringDiscovery` holds the source's own packets (ARA `core/ara-router.h`, `onLocalData`, `holds`; `docs/fidelity.md` row "Data packets waiting for a route"). Route failure and backtracking are in the core (`onRouteFailure`). The mark is still an ns-3 packet tag with no bytes: `DataFlagTag`, "a real implementation carries the flag in a data header" (ARA `ns3/model/ara-header.h`, lines 64 to 66) |
| HOPNET | (swarm-routing/.github#268) "not runnable on ns-3 (no module yet)" | an ns-3 module exists and carries the `DataHeader` after the UDP header (HOPNET `ns3/model/hopnet-header.h`, line 74; HOPNET AGENTS.md "What this repo is") |
| AntHocNet | "its ns-3 adapter passes none today" (the previous hop) | still true: `nextHopForData(ToCore(dst))` with no previous hop, under `TODO(A1)` (AntHocNet `ns3/anthocnet/model/anthocnet-routing-protocol.cc`, lines 1186 to 1189). What changed: the adapter now derives from `SwarmRoutingBase` (`anthocnet-routing-protocol.h`, line 74) |

### Terms, each used in one sense below

- **Overlay**: the addresses applications use to name nodes, and the virtual
  device that carries them.
- **Underlay**: the radio (or wired) interface a node reaches its one-hop
  neighbours over.
- **Frame**: one datagram the daemon sends on the underlay.
- **Inner packet**: the application's IP packet, as the daemon read it from
  the overlay device.
- **Data header**: the bytes a protocol puts on every data packet (Termite's
  header, BeeAdHoc's trailer, HOPNET's data header). The name of the type in
  each core is in the table.
- **Node id**: what `swarm::core::NodeAddress` holds; its meaning is the ADR
  for swarm-routing/ns3-swarm-core#72
  ([ADR-0002](0002-node-address-is-an-opaque-node-id.md), "the identity ADR").

## Decision

1. **The forwarder is a TUN device plus an underlay socket, for every
   protocol.** A TUN device is a virtual layer-3 interface whose packets a
   program reads and writes. The daemon creates one (the overlay device),
   gives it the node's overlay addresses, reads each inner packet, asks the
   core for a decision through the driver contract
   (swarm-routing/ns3-swarm-core#71), and sends the packet to the chosen
   neighbour in a frame. The destination's daemon writes the inner packet
   into its own overlay device and the kernel delivers it.
   *Reason:* it is the only one of the four candidates (rtnetlink alone,
   NFQUEUE, eBPF, TUN) that gives all five protocols what the table below
   lists without a second mechanism: the packet is in the daemon, so it can
   be held, sent to a next hop drawn for it, and given a data header.
   Unmodified `ping` and `iperf` work, because they see an ordinary interface
   (the criterion of swarm-routing/AntHocNet#152). It needs no netfilter
   rule, no `ip rule` and no route of the daemon's own, which makes
   decision 12 short.

2. **The underlay is UDP over IPv6 link-local addresses, for control
   messages and for data alike, one socket per underlay interface.** A
   link-local address is the `fe80::/10` address every IPv6 interface has as
   soon as it is up. A one-hop broadcast becomes a send to a link-local
   multicast group on that interface, hop limit 1, multicast loopback off.
   The overlay carries IPv4 and IPv6 inner packets alike (the version is the
   first four bits of the inner packet). This is the arrangement Babel uses
   ([RFC 8966](https://www.rfc-editor.org/rfc/rfc8966)).
   *Reason:* the radio interfaces need no addressing plan; IPv4 broadcast,
   the dual-stack socket question and the "two subnets" set-up of the
   namespace test (swarm-routing/.github#256, comment of 2026-11-16) do not
   arise; a node with several radios has one identity whatever its
   interfaces do (the identity ADR). UDP, and not raw layer-2 frames, because
   swarm-routing/.github#256's first slice runs unprivileged over UDP and one
   carrier for control and data is less code than two.
   *Cost:* IPv6 must be enabled on every node's underlay interface.

3. **Every frame starts with a short host header that names its sender: a
   frame version byte, a kind byte (control or data), and the sender's node
   id (4 bytes, little-endian, as every core encodes an id). The previous hop
   of a packet is that sender id.** A control frame then carries the
   protocol's control message exactly as the core's codec encodes it. A data
   frame then carries the length of the data header (2 bytes), the data
   header as the core's codec encodes it (empty for a protocol that has
   none), and the inner packet.
   *Reason:* on ns-3 the receiver learns who sent a control message from the
   IPv4 source address (`RecvControl(p, from, receiver)`, ns3-swarm-core
   `model/swarm-routing-base.h`) and who sent a data packet from a packet
   tag that stands in for the MAC source address (`SwarmPrevHopTag`, same
   file: "A real implementation reads it from the link layer's source
   address"). A link-local source address is neither a node id nor derivable
   from one, and not every control message names its sender (ARA's ant
   carries its creator, not the node that relayed it: ARA `core/ara-ant.h`).
   Six bytes per frame give every protocol the same answer and make the
   carrier replaceable: nothing above the frame depends on IPv6.
   The header is the host's, has its own version byte (org rule 3), and is
   never seen by a core.

4. **"The same bytes everywhere" means: the bytes a core's codec produces are
   the same on every host; where a host puts them is the host's.** Three
   rules:
   - a control message is byte-identical on ns-3, on the discrete-event host
     and in the daemon's frame (after the host header);
   - a data header is byte-identical for the same decision on every host;
   - the place of the data header differs and is recorded per host: on ns-3
     after the application's UDP header, inside the IPv4 packet (so the
     harness's flow classifier still sees a data packet: Termite
     `ns3/model/termite-header.h`; BeeAdHoc `docs/fidelity.md` line 28;
     HOPNET `ns3/model/hopnet-header.h`); in the daemon, in the frame, in
     front of the inner packet, which is not modified; on the discrete-event
     host, next to an opaque payload (it has no IP packet).
   *Reason:* this settles Termite's header placement without changing the
   ns-3 layout and without a wire-version bump. The premise of the issue
   ("placement, which no stock stack delivers") describes a layout that is no
   longer on `main`. Putting the header inside the application's packet on
   Linux would mean rewriting the IP length and the UDP length and checksum
   at the source and undoing it at the destination, for no gain: no harness
   classifies flows on the underlay. The check that makes the rule concrete
   is in "Consequences".

5. **Held packets wait in the daemon, in one bounded queue per node, with the
   limits each protocol already has on ns-3; none is typed again in the
   daemon.** The limits are read from the protocol's driver (the table
   gives today's values and where they are). A packet that waits longer than
   its limit, or arrives when the queue is full, is dropped and counted by
   cause. When the daemon stops, it drops what it holds and counts it as
   node-down, as `SwarmRoutingBase` does at shutdown ("drops every deferred
   packet as NodeDown", `model/swarm-routing-base.h`). When the daemon is
   killed, the held packets are lost with it; nothing of them stays in the
   kernel.
   *Reason:* org rule 7, and no per-host tuning of a protocol's behaviour.
   The example numbers of the outside proposal (32 per destination, 512 in
   total, 2 s) are not adopted.

6. **The daemon decrements the inner packet's TTL (IPv4) or hop limit (IPv6)
   at every forwarding node and drops the packet at zero, counted as a TTL
   drop.** For IPv4 it updates the header checksum; nothing else in the
   inner packet changes.
   *Reason:* AntHocNet on ns-3 still relies on the TTL to end a loop
   (`TODO(A1)`, above), and swarm-routing/.github#268's RT-3 checks that "no
   packet dies of TTL". Without this the check would pass by construction.
   An ICMP "time exceeded" answer is not generated (deferred: no scene needs
   it).

7. **The overlay device's MTU is computed, not typed: the underlay MTU, minus
   the outer headers (40 bytes IPv6, 8 bytes UDP), minus the host header and
   the data-header length (8 bytes), minus a budget for the protocol's data
   header.** The budget is the largest data header the core can build, read
   from the core's own constants, but never so large that the overlay MTU
   falls below 1280 bytes, the smallest MTU an IPv6 link may have
   ([RFC 8200](https://www.rfc-editor.org/rfc/rfc8200), section 5). A frame
   that is still larger than the underlay MTU (a data header beyond the
   budget) leaves as a fragmented outer datagram, which the sending kernel
   does for a UDP socket and the neighbour's kernel reassembles (*to verify
   on a kernel*), and is counted. Every node of one network uses the same
   overlay MTU; the daemon refuses to start when its underlay MTU cannot
   carry it.
   With a 1500-byte underlay: AntHocNet 1444; Termite 1421 (a 23-byte
   header, `kPacketWireSize`); BeeAdHoc 1342 (a trailer of at most 102
   bytes: 8 fixed, 4 per route node for `maxHops + 1` = 11 nodes, and one
   piggybacked forager of 6 + 44; `core/beeadhoc-trailer.h`,
   `core/beeadhoc-config.h`); HOPNET 1280 (its header is 17 bytes plus 4 per
   route node and `maxPathLength` is 100, so the budget is capped at 164
   bytes, a route of 36 nodes; `core/hopnet-data.h`, `core/hopnet-config.h`
   line 57); ARA 1444 minus the data header decision 14 asks ARA to define.
   *Reason:* an inner packet an application sends then always fits, so the
   daemon needs no path-MTU answer (ICMPv6 "packet too big") in the common
   case; only local applications write into the overlay device. A fragment
   the application's own kernel made (an IPv4 datagram larger than the
   overlay MTU without "don't fragment") is an inner packet like any other:
   each fragment is forwarded on its own, with its own data header. This
   differs from ns-3, where IP fragments after the header was added and
   "only a first fragment carries the header" (Termite
   `ns3/model/termite-routing-protocol.cc`, line 205); the difference is
   recorded here and has no scene (the scenes send 64-byte payloads).

8. **A send failure is reported to the core, with the packet, only when the
   kernel reports one for that frame.** Two cases are expected: the send
   call fails at once (the interface is down); or an error arrives on the
   socket's error queue with the datagram attached (the neighbour's address
   could not be resolved, or the neighbour's kernel answered that no program
   listens on the port). Both need the socket option that turns the error
   queue on (*to verify on a kernel*, including what the kernel returns for
   a neighbour whose resolution fails). A frame the radio gave up on after
   its retries is not reported to a UDP socket, so on a real radio the
   common failure is silent.
   *Reason and limit:* AntHocNet (`reportTxFailure(next, dataDest)`) and
   HOPNET (`onDataSendFailure(DataHeader&, nextHop)`) want the failed packet
   back; ARA's backtracking and BeeAdHoc's and Termite's link-failure
   handling start from the same signal. The data plane passes on what the
   kernel knows and invents nothing: a per-hop acknowledgement added by the
   host would be new control traffic, which is a change of behaviour and
   swarm-routing/ns3-swarm-core#73's decision, not this one. What each
   protocol does when the signal is absent is the last column of the table.
   The other link signals a Linux host has (interface and carrier state and
   neighbour-table events from rtnetlink, station statistics from nl80211)
   are tiers of swarm-routing/ns3-swarm-core#73's port; the daemon listens
   for them, and this ADR does not rank them.

9. **Overhearing has one optional mechanism and a stated absence.** Where the
   interface and its driver allow it, the daemon opens a packet socket on
   the underlay interface in promiscuous mode, filtered to the daemon's UDP
   port, and hands each frame addressed to another node to the driver, which
   needs no more than the frame (Termite's header names the previous and the
   next hop itself: `core/termite-packet.h`). Where they do not (many Wi-Fi
   drivers deliver other stations' unicast frames only in monitor mode,
   *to verify on a driver*; a pair of virtual Ethernet devices has no third
   listener), the host answers "overhearing: absent" to the capability query
   of swarm-routing/ns3-swarm-core#73, and Termite runs learning only from
   frames addressed to it, with that written in its start-up log and in the
   run's `##CONFIG##` line.
   *Reason:* [R05] §4.2.2 assumes promiscuous reception (Termite
   `ns3/model/termite-routing-protocol.cc`, line 64); a host cannot promise
   it. Sending Termite's data frames to the multicast group, so that every
   neighbour receives them, was considered and is not adopted (see
   "Alternatives").

10. **The battery reading comes from the host when it has one, and is
    "absent" otherwise.** Absent is NaN on `IEnergy`
    (ns3-swarm-core `core/swarm-ports.h`), which BeeAdHoc already reads as a
    full battery: "No battery data (NaN) reads full, 7 (§13.1.2)" (BeeAdHoc
    `core/beeadhoc-forager.h`, line 48). Where a reading comes from on a
    board (a power-supply entry under `/sys`) is the daemon's, not decided
    here.

11. **The kernel mode is a fast path on top of decision 1, default off, for
    AntHocNet only, and it is exactly what F2 measures: one next hop per
    destination, changed only when the route changes.** In that mode the
    daemon also installs, for each destination it has a route to, one kernel
    host route through the best neighbour, marked with a routing-protocol
    number of its own; packets that match bypass the daemon and are
    forwarded by the kernel, without a frame. A destination with no such
    route still falls through to the overlay device, so discovery and the
    first hold work as in decision 1.
    What is lost against the per-packet choice, to be written in AntHocNet's
    fidelity sheet when the mode is built: the draw per packet (AntHocNet
    ADR-0010), the exclusion of the previous hop, the holds while a route
    reconverges, the failed packet on a send failure, and the daemon's count
    of forwarded packets.
    Every node of a network runs the same mode, as for the MTU (decision
    7): a packet the kernel forwards carries no frame, so a baseline node on
    its path could not relay it. Kernel mode needs the kernel's own
    forwarding switched on, which the baseline never touches (decision 12).
    The daemon switches it on only for the address families the overlay
    carries (the IPv4 and IPv6 forwarding settings; scoped to the overlay
    and underlay interfaces where the kernel allows it per interface,
    *to verify on a kernel*). Before changing a setting it writes the
    previous value to a state file; a clean stop restores every value from
    that file and removes it; after a kill, the next start finds the file,
    restores the values first and only then proceeds. The kernel routes are
    removed the same way, by their routing-protocol number (question 5).
    None of this is tested: the mode is not built before F2, and the first
    change that builds it tests the restore after a kill, as
    swarm-routing/.github#256 asks for the baseline.
    It is not built before F2's result is recorded on
    swarm-routing/.github#269, and not at all if F2 fails its threshold.
    *Reason:* the only kernel mode worth building is the one whose cost has
    been measured. A weighted multipath route (weights from the pheromone
    shares) is a different mechanism: the kernel picks among its next hops
    per flow, by a hash, not per packet, and F2 does not measure it; it is
    deferred until a switch of its own measures it on ns-3. For ARA the mode
    is not meaningful (every data packet deposits pheromone and is checked
    for duplicates, `onDataSent`, `onDataReceived`, `admitData`; a kernel
    that forwards past the daemon lets the trail evaporate while it is in
    use). For BeeAdHoc, Termite and HOPNET it is impossible: each puts a
    data header on every packet.

12. **Privileges and cleanup.** The daemon needs the capability to administer
    network devices (`CAP_NET_ADMIN`) to create the overlay device and set
    its addresses and MTU. In the baseline it needs it only while starting
    and gives it up afterwards. The optional packet socket of decision 9
    needs `CAP_NET_RAW` when it is opened; the kernel mode of decision 11
    keeps `CAP_NET_ADMIN`. The overlay device is not persistent: the kernel
    removes it, its addresses and its connected route when the process that
    holds it ends, however it ends. The baseline creates nothing else in the
    kernel: no netfilter rule, no `ip rule`, no route with the daemon's
    number, no changed sysctl. In kernel mode every route carries the
    daemon's routing-protocol number, so one flush by that number removes
    them; the scene runner does that after a kill and checks the table is
    clean, and the daemon does it at its own start before installing
    anything.
    *Reason:* the criterion of swarm-routing/AntHocNet#152 and
    swarm-routing/.github#256 ("leaves no TUN device, queue rule, `ip rule`
    or route"). Tested here: on kernel 6.18 a TUN device created without the
    persistent flag is gone after `SIGKILL` of its owner.

13. **rtnetlink keeps two of the three side roles proposed, not the third.**
    Kept: link and neighbour events as a link signal (decision 8), and the
    kernel mode's routes (decision 11), with a hysteresis so pheromone
    evaporation does not produce a message per update; the gate's numbers
    are fixed when the mode is built. Not kept: mirroring the best route
    into the kernel table in the baseline.
    *Reason:* with decision 1 the kernel's route for the overlay is the
    connected prefix on the overlay device; a second, mirrored route would
    either be ignored or would forward packets past the daemon, and it is
    one more thing to clean up. The protocol's state is shown where
    swarm-routing/.github#256 already puts it (the router observer's
    counters, "a log line or a socket").

14. **What is missing in a protocol repository is that repository's work, by
    an issue filed when this ADR is accepted** (the list is in
    "Consequences"). Two are needed before the data plane can carry the
    protocol at all: ARA's data flags as bytes, and AntHocNet's previous hop
    on ns-3 if the two hosts are to decide alike.

## The completed table

One row per requirement. "Absent" is what the host answers and what the
protocol then does; none of it changes a protocol's rules.

| Protocol | Requirement (where it is in the code) | Mechanism on Linux | When it is unavailable |
|---|---|---|---|
| AntHocNet | a next hop drawn per packet: `nextHopForData(dest, prevHop)` (`core/include/anthocnet/core/ant_router_logic.h`, line 176) | decision 1: the driver is asked for every inner packet | cannot be absent in the baseline; in kernel mode it is the recorded deviation (decision 11) |
| AntHocNet | the previous hop, to exclude it (ADR-0010) | the sender id of the frame (decision 3) | always available. ns-3 passes none today (`TODO(A1)`), so the daemon and ns-3 can decide differently until AntHocNet's issue lands |
| AntHocNet | hold: 64 packets, 3 s (`QueueTimeout`), a reconvergence hold at most 0.2 s (`ReconvHoldCap`) (`ns3/anthocnet/model/anthocnet-rqueue.h` `ConfigurePendingQueue`; `anthocnet-routing-protocol.cc` lines 97 to 99) | the daemon's queue with the same limits and hold classes (decision 5) | cannot be absent |
| AntHocNet | the failed packet back: `reportTxFailure(next, dataDest)` | decision 8 | the packet is lost; the neighbour is declared lost by the hello timeout, `helloInterval x allowedHelloLoss` = 2 s (`core/include/anthocnet/core/config.h`), the mandatory detector (ADR-0008) |
| AntHocNet | nothing on the wire with data | a data frame with an empty data header | — |
| ARA | the best trail, no draw: `nextHop(dest)`, ties to the lowest address (`core/ara-router.h`) | decision 1 | — |
| ARA | the previous hop of every data packet, and a deposit per packet: `onDataReceived(source, prev)`, `onDataSent(dest, next)` | the sender id of the frame; the driver calls both for every frame | always available |
| ARA | the duplicate key: source, destination, IP protocol, IP identification (`DataKey`, `core/ara-router.h`; `docs/fidelity.md` row "Data packet 'sequence number'") | source, destination and protocol from the inner packet; **the identification is not usable**: an IPv6 packet has none outside a fragment header, fragments of one IPv4 datagram share one, and Linux may send 0 in every unfragmentable IPv4 packet of an unconnected socket (*to verify on a kernel*) | every packet after the first of a flow could read as a duplicate and be sent back. ARA's data header must carry a sequence number of its own, as [G02] §3.2 has it (decision 14) |
| ARA | the DUPLICATE_ERROR and backtrack flags on a data packet: an ns-3 tag with no bytes (`ns3/model/ara-header.h`, `DataFlagTag`) | a data header in the frame, once ARA's core defines one | without it ARA cannot return a duplicate or backtrack off ns-3 |
| ARA | the failed packet, to take another trail or backtrack: `onRouteFailure(key, failed, cameFrom)` | decision 8 | no failure is seen: ARA has no periodic message to time a neighbour out with (swarm-routing/ns3-swarm-core#73). The trail evaporates and the source discovers again when its packets find no trail |
| ARA | hold: none by default; with `bufferDuringDiscovery`, 64 packets, 30 s (`docs/fidelity.md` row "Data held during discovery") | the daemon's queue, used only with the switch on | — |
| BeeAdHoc | the source draws a forager, relays follow the route: `routeOutput`, `forwardData(trailer)`, `deliverData` (`core/beeadhoc-router.h`) | decision 1; the relay rewrites the trailer (its battery level into `info`) before it forwards | — |
| BeeAdHoc | the source route on every packet: `RouteTrailer`, 8 bytes plus 4 per route node, plus an optional piggybacked forager (`core/beeadhoc-trailer.h`) | the data header of the frame (decision 4) | cannot be absent |
| BeeAdHoc | hold at the source until a scout returns: 64 packets, 30 s (`docs/fidelity.md`, line 27) | the daemon's queue | — |
| BeeAdHoc | a battery reading: `IEnergy` | decision 10 | NaN, read as full (level 7), as the report's module does without battery data (`docs/fidelity.md` row "A node without battery data reports a full battery") |
| BeeAdHoc | a failed next link: `onLinkFailure(neighbor)` (`docs/fidelity.md` row "A failed next link", a repo choice) | decision 8 | no failure is seen (no periodic message); foragers over a dead link stay until they age out (`MAXAGE`) |
| Termite | a draw per packet (eq. 4.3): `originate`, `onReceive` | decision 1 | — |
| Termite | a 23-byte header on every data packet, with previous and next hop in it (`core/termite-packet.h`) | the data header of the frame (decision 4); the previous hop the core uses is the one in its own header | cannot be absent |
| Termite | overhearing neighbours' data ([R05] §4.2.2; `ns3/model/termite-routing-protocol.cc` lines 64 to 72 and 196 to 209) | a packet socket in promiscuous mode (decision 9) | "absent": Termite learns only from frames addressed to it; stated at start and in `##CONFIG##` |
| Termite | hold: none. The core can answer Queue, and the ns-3 adapter drops it: "the core's route request […] is its next slice, so the packet is dropped now" (`termite-routing-protocol.cc`, lines 188 and 189) | the daemon does as the ns-3 adapter does, until swarm-routing/Termite#6 changes both | — |
| Termite | a lost link: `onLinkLost` (swarm-routing/ns3-swarm-core#71's table) | decision 8 | no failure is seen; pheromone toward the dead neighbour decays |
| HOPNET | a next hop per packet inside and between zones, with the previous hop: `routeData(header, from)` (`core/hopnet-interzone.h`, line 109) | decision 1; `from` is the sender id of the frame | — |
| HOPNET | its data header: 17 bytes plus 4 per route node (`core/hopnet-data.h`) | the data header of the frame (decision 4) | cannot be absent |
| HOPNET | hold: `repairBufferCapacity` 64 packets, `repairBufferTimeout` 3 s (`core/hopnet-config.h`, lines 91 and 92) | the daemon's queue | — |
| HOPNET | the failed packet back: `onDataSendFailure(header, nextHop)` | decision 8 | the packet is lost; the neighbour times out after `internalAntPeriod x allowedAntLoss` = 6 s (`core/hopnet-config.h`, lines 28, 33 and 140; swarm-routing/.github#268 still says 2 s) and local repair then starts from the next packet that finds no pheromone toward its next Path node (reading (r), HOPNET ADR-0002) |

### The same requirements on the other hosts

| Requirement | ns-3 (the reference) | Discrete-event host | Linux daemon |
|---|---|---|---|
| Control message | UDP payload over IPv4, to the subnet broadcast or a neighbour | the message's bytes, handed to each neighbour by the model | the frame's payload after the host header (decision 3) |
| Who sent it | the IPv4 source address | the model names the sender | the sender id in the host header |
| Data header | after the application's UDP header, inside the IPv4 packet | the header's bytes next to an opaque payload | in the frame, in front of the unmodified inner packet |
| Previous hop of data | `SwarmPrevHopTag`, no bytes | the model names the sender | the sender id in the host header |
| Send failure | the Wi-Fi MAC's retry limit, with the packet (`OnTxFailure`) | the model returns the message (swarm-core's testbench: "a unicast to a node that is not a neighbour comes back", `core/testbench/swarm-testbench.h`) | only what the kernel reports (decision 8) |
| Overhearing | a promiscuous protocol handler | the model delivers to every node in range | a packet socket, or absent (decision 9) |
| Battery | the harness's energy model | a model, or absent | a gauge, or absent (decision 10) |

## The scenes the data plane must pass, per protocol

The scenes are swarm-routing/.github#268's; the rung is 4 (the daemon in
network namespaces). "Must pass" means the checker prints PASS with this data
plane; "expected failure" keeps swarm-routing/.github#268's meaning (it turns into an error the
day it passes); "cannot" is a limit of the data plane or of the rung.

| Protocol | Must pass | Expected failure, and what ends it | Cannot, and why |
|---|---|---|---|
| AntHocNet | RT-1 to RT-9. RT-3, RT-5 and RT-6 on its hello timeout alone (swarm-routing/.github#256's criterion); a cold `ping` loses nothing, since the first packet is held | none | nothing |
| ARA | RT-1, RT-2, RT-4, RT-7, once its data header exists (decision 14). A cold `ping` loses its first packets by design (no buffering) | RT-3, RT-5, RT-6, RT-8: swarm-routing/.github#268 names swarm-routing/ARA#5; the handling is now in the core, and what is missing off ns-3 is the failure signal (decision 8; the beacon decision of swarm-routing/ns3-swarm-core#73) | nothing |
| BeeAdHoc | RT-1, RT-2, RT-4, RT-7: swarm-routing/.github#268's condition "once the source route is on the wire" is met on `main` | RT-3, RT-5, RT-6, RT-8: the failure signal, as ARA | the battery levels of a route are all 7 without a gauge, so no scene on this rung exercises the energy rating |
| Termite | none today | every scene: "a fresh network delivers nothing until a node can learn a destination it has not heard from" (swarm-routing/Termite#6; Termite AGENTS.md says the same on `main`). When it lands: RT-1, RT-2, RT-4 and RT-7 must pass without overhearing | overhearing, on this rung: each link is its own pair of virtual devices, so a third node hears nothing. It can first be exercised on rung 5 or 6 |
| HOPNET | RT-1, RT-2, RT-4, RT-7, once it has a driver and passes them on rung 2 | RT-3, RT-5, RT-6, RT-8, until rung 2 shows them passing: nothing has run HOPNET on a repair scene, and its ns-3 numbers do not count (HOPNET `docs/anchor-verdict.md`) | nothing known |

RT-4's "address no node has" is an overlay address whose node id no node
holds (the identity ADR). Kernel mode, if built, must pass RT-1, RT-2 and
RT-7 for AntHocNet and is expected to do worse on the repair scenes; by how
much is F2's number.

## Consequences

- **The two acceptance checks of swarm-routing/.github#256 that touch bytes
  become precise.** "The daemon's control bytes are the ones the ns-3 module
  sends" reads: the payload of a control frame after the 6-byte host header
  decodes with the core's codec and equals, field by field, the message in
  ns-3's capture of the same scene. For data: the data header of a frame
  decodes with the core's codec and equals the header ns-3 carries for the
  same hop. The one exception is ARA's 2002 version, whose data flag stays
  an ns-3 tag with no header bytes (question 4): ns-3 carries no header to
  compare with, so that version has no data-byte check, and what replaces
  it is decided with swarm-routing/ARA#189.
  swarm-routing/.github#256's text ("UDP (broadcast and unicast)") becomes
  "multicast and unicast".
- **Issues filed on acceptance** (acceptance criterion 4 of the issue;
  2026-11-16): swarm-routing/AntHocNet#1085 and swarm-routing/ARA#189. The
  other items are notes for each protocol's driver, written when the driver
  is:
  - AntHocNet: pass the previous hop on ns-3 (the `TODO(A1)`); it changes
    ns-3 output, so it is an A/B there, not a silent fix.
  - ARA: a versioned data header in `core/` (the two flags and a sequence
    number), used by the discrete-event host and the daemon; what ns-3 does
    with it is open question 4.
  - BeeAdHoc: none for the wire (`RouteTrailer` exists); its driver states
    the battery's "absent".
  - Termite: its fidelity sheet states what Termite does without
    overhearing; its driver's Queue handling follows swarm-routing/Termite#6.
  - HOPNET: its driver states what it does when no send failure is reported;
    swarm-routing/.github#268's bound for HOPNET is recomputed from the
    constants (6 s, not 2 s).
  - swarm-routing/.github#268: section 5's table is regenerated (BeeAdHoc's
    and HOPNET's cells, ARA's reason).
  - swarm-routing/AntHocNet#152: points here; its crash-cleanup and
    privilege criteria are decision 12.
- **An application reaches nodes, not networks.** The overlay prefix is the
  set of node addresses. A packet read from the overlay device whose
  destination is outside it, or is a multicast or broadcast address, is
  dropped and counted. A gateway to another network is not in this design.
- **The kernel's own neighbour discovery runs on the underlay.** Sending UDP
  to a link-local address makes the kernel resolve and re-check that
  neighbour with its own solicitations. That traffic is on the air and is
  not the protocol's; swarm-routing/.github#269's F3 (air cost) has to count
  it on rungs 4 and 6 (*to verify on a kernel* how much it is, and whether
  the daemon should supply the neighbour entries itself from what it hears).
- **Overhead per data packet** on the underlay is 56 bytes of host
  encapsulation plus the protocol's data header. It is reported apart from
  the protocol's own overhead; results off ns-3 are qualitative in any case.
- **One daemon, one protocol, one port per link.** Two different protocols
  on one link need different ports; the frame's version and kind bytes are
  the guard against a wrong parse, not a discriminator.
- **Ruled out by this ADR:** a netfilter dependency in the baseline; any
  second implementation of a core's decision outside the core; a host-level
  acknowledgement or beacon introduced by the data plane.

## Alternatives considered

- **rtnetlink alone** (the plan of swarm-routing/AntHocNet#65 and swarm-routing/AntHocNet#152).
  Rejected as the baseline: the kernel answers "unreachable" before the
  daemon sees a packet, picks among several next hops per flow, and cannot
  add a data header. It survives as decision 11.
- **NFQUEUE (a netfilter queue) with rtnetlink**, as a second mode for
  applications that bind to the radio interface's own address. Deferred, not
  built: it needs nftables on every node, a rule that must be removed after
  a crash (a queue rule with no listener drops or passes every packet,
  depending on a flag), and payload rewriting with checksum fixes for the
  three protocols with a data header. No scene and no user asks for it; it
  returns if a deployment cannot use overlay addresses.
- **eBPF** (a traffic-control or XDP program drawing the next hop from a
  table the daemon shares with it). Rejected for now: it is a second
  implementation of the draw, in the kernel, that would have to stay
  byte-identical with the core's and take its randomness from somewhere
  other than the core's port; it needs a recent kernel and more privileges;
  and it gives the daemon no packet to hold. It is reconsidered only if
  swarm-routing/.github#269's F4 (CPU per decision on arm64) fails because
  of the copy through user space.
- **Raw layer-2 frames on the underlay** (an EtherType of the two IEEE
  reserves for local experiments; `ETH_P_802_EX1` is 0x88B5). Rejected: it
  needs `CAP_NET_RAW` for the first slice that swarm-routing/.github#256
  wants unprivileged, and an experimental EtherType is not something to
  install on other people's networks. Its one advantage, the source MAC
  address as previous hop, is covered by decision 3.
- **An IPv4 underlay** (each radio interface in a subnet, subnet broadcast,
  as on ns-3). Rejected as the baseline: every radio interface needs an
  address from a plan, a node with two radios has two addresses and the
  question of swarm-routing/AntHocNet#218 returns. Because of decision 3 it
  can be added later as a second carrier without touching a core, if a
  network cannot run IPv6.
- **Termite's header as an IPv6 Hop-by-Hop option.** Rejected:
  [RFC 8200](https://www.rfc-editor.org/rfc/rfc8200) (section 4) does not
  let a node on the path insert or delete extension headers, Termite's
  header is rewritten at every hop, and the daemon is not the source host of
  the inner packet.
- **Termite's data frames sent to the multicast group**, so that every
  neighbour hears them on any hardware. Not adopted: a multicast frame is
  not acknowledged or retried by a Wi-Fi MAC and goes out at a basic rate,
  so it would change delivery and remove the little failure signal there is.
  It could be measured later as a default-off switch in Termite's
  repository.
- **Changing the ns-3 layout** so the data header sits where the daemon puts
  it. Rejected: it changes ns-3 output for three protocols for no
  measurement gain and would need the harness to classify flows differently.
- **Mirroring the best route into the kernel table** in the baseline:
  decision 13.
- **A weighted multipath route as the kernel mode:** deferred, decision 11.

## Questions decided with this ADR

Each question below was put to the maintainer with a recommendation; on
2026-11-16 every recommendation was accepted, so each "Recommendation" is
the decision.

1. **Which UDP port and multicast group?**
   Recommendation: a private port per protocol, by default the one that
   protocol's ns-3 module uses for control (AntHocNet 6900,
   `anthocnet-routing-protocol.cc` line 52; BeeAdHoc 5544, its
   `docs/fidelity.md`), configurable; and the all-nodes group `ff02::1`,
   which needs no membership. Not UDP port 269 and not `ff02::6d`:
   [RFC 5498](https://www.rfc-editor.org/rfc/rfc5498) assigns them to MANET
   protocols and ties their use to the packet format of RFC 5444, which
   these frames are not.
   Reason: the same port as ns-3 keeps the capture comparison trivial, and
   `ff02::1` is the least set-up. If waking every IPv6 host on a shared link
   matters on a real network, a group of the daemon's own is a later change
   to one constant.
2. **How does the scene runner take a link or a node down in namespaces?**
   Recommendation: silently, by dropping everything on the link (`tc netem`,
   100 % loss), for link events and for the time a node is down; the loud
   forms (`ip link set down`, or killing a daemon whose kernel then answers
   "port unreachable") run too and are reported, not gated.
   Reason: with decision 8 the kernel reports a loud failure for the very
   frame that failed, which no radio does. A daemon that passes RT-3 only
   because of that signal would fail on rung 6. swarm-routing/.github#256's
   criterion "on its hello timeout alone" needs the silent form anyway.
3. **Does the daemon pass AntHocNet the previous hop before ns-3 does?**
   Recommendation: yes. ADR-0010 makes the exclusion unconditional; the ns-3
   adapter's omission is a known gap, and the browser host already passes it
   (swarm-routing/.github#250, first comment). The difference between the
   two hosts is written in the daemon's notes until AntHocNet's issue lands.
   Reason: copying a gap into a new host to match the reference would be the
   wrong direction, and results off ns-3 are qualitative.
4. **Does ARA's ns-3 module carry the new data header, or keep its tag?**
   Recommendation: keep the tag on ns-3 for the 2002 version, record it as
   the one exception to decision 4 (ARA's data packets are a few bytes
   shorter on ns-3 than on any real node), and move ns-3 to the header with
   the 2003 version (swarm-routing/ARA#27).
   Reason: the tag was a maintainer decision of 2026-11-13 on
   swarm-routing/ARA#5 and three pre-registered anchors were read with it;
   changing the bytes now changes those outputs for no decision.
5. **The routing-protocol number of the kernel mode's routes.**
   Recommendation: decide it when the mode is built, as a configurable
   value with a default checked against the names iproute2 ships
   (`rt_protos`). The proposal's 199 is in the range left to user space;
   whether another daemon already uses it could not be checked here
   (iproute2 is not installed in this session).
6. **Is the kernel mode kept in this ADR at all, before F2 has run?**
   Recommendation: yes, as decision 11 words it (what it is, what it loses,
   that it waits for F2).
   Reason: swarm-routing/.github#255's scope asks for it, and writing down
   that the measured mode is the single-next-hop one stops a multipath
   variant from being built on an untested assumption.

## Where each open point of the issue's comments went

| Point (comment of 2026-11-16) | Resolved or deferred |
|---|---|
| 1. Termite's header placement | resolved: decision 4 (the code on `main` already differs from the issue) |
| 2. The previous hop | resolved: decision 3; the id itself is the identity ADR |
| 3. Overhearing | resolved: decision 9 |
| 4. HOPNET, and the failed packet back | resolved as a limit: decision 8; the signal's tiers are deferred to swarm-routing/ns3-swarm-core#73 |
| 5. MTU and fragments | resolved: decision 7 |
| 6. Broadcast | resolved: decision 2; the group is open question 1 |
| 7. Kernel weights are per flow | resolved: decision 11 (single next hop; multipath deferred) |
| Port "26900" / RFC 5498 | open question 1 |
| `ETH_P_EXPERIMENTAL` | raw frames rejected ("Alternatives") |
| The sketches' API names | none is used; the contract is swarm-routing/ns3-swarm-core#71 |
| Neighbor Solicitations into the overlay device | does not apply (a TUN device is layer 3); the underlay's own neighbour discovery is in "Consequences" |
| `rtm_protocol` 199 | open question 5 |
| Where the code lives | not reopened (swarm-routing/.github#253 item 2) |
| DAD on the overlay device | the daemon adds its overlay IPv6 address with duplicate address detection off, since nothing else is on a TUN device ([RFC 4862](https://www.rfc-editor.org/rfc/rfc4862) is why an address is otherwise tentative for about a second); *to verify on a kernel*, as is waiting for the underlay's link-local address to leave the tentative state before the first send |
| Multicast details (loopback off, hop limit 1, the interface in every send) | decision 2 |
| ICMPv6 "packet too big" | not needed in the common case (decision 7); deferred |
| IPv6 kernel multipath | deferred with multipath (decision 11) |
| The link-local underlay | adopted: decision 2 |
| Hop-by-Hop option for Termite | rejected ("Alternatives") |
| Flow label; key-derived addresses | not data-plane matters: swarm-routing/.github#265; the identity ADR |
| eBPF | rejected for now ("Alternatives") |
| NFQUEUE as a second mode | deferred ("Alternatives") |

## Not verified

- Everything marked *to verify on a kernel* or *on a driver*: the error
  queue's content for an unresolved neighbour and for a closed port; the
  kernel fragmenting an oversized outer datagram; the IPv4 identification
  Linux writes; promiscuous reception on Wi-Fi; the cost of the kernel's
  neighbour discovery; duplicate address detection flags; whether a hosted
  CI runner gives a new network namespace working IPv6 link-local addresses.
- Tested in this session (kernel 6.18, as root, in a new network namespace):
  only that a non-persistent TUN device disappears when its owner is killed.
- HOPNET on a repair scene: no run exists on any host.
