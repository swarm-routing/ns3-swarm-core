# 0002. A node address is an opaque 32-bit node id; the host maps addresses to it by a rule, and no wire format changes

- **Status:** accepted (maintainer, 2026-11-16: the draft and every recommendation under "Questions decided with this ADR")
- **Date:** 2026-11-16
- **Issue:** swarm-routing/ns3-swarm-core#72 (part of swarm-routing/ns3-swarm-core#82 and swarm-routing/.github#250)

Code facts were read on `origin/main` on 2026-11-16 (ns3-swarm-core `671553b`, AntHocNet
`43db321`, ARA `15be777`, BeeAdHoc `06dfc76`, Termite `de0552b`, HOPNET
`dfc6e5c`). It is written together with the data-plane ADR for
swarm-routing/.github#255 ([ADR-0003](0003-forward-data-in-user-space-off-ns3.md), "the data-plane ADR") and uses its terms: the
**overlay** is the set of addresses applications use to name nodes, the
**underlay** is the interface a node reaches its one-hop neighbours over,
and a **frame** is one datagram the daemon sends on the underlay.

## Context

`core/swarm-types.h` defines the type every core names a node with:

```
/// Network-layer node address: the width of ns-3's 32-bit IPv4 address payload.
using NodeAddress = std::int32_t;
inline constexpr NodeAddress kInvalidAddress = -1;
```

AntHocNet's core uses the same type since it moved onto swarm-core
(`core/include/anthocnet/core/types.h`, lines 24 and 28). The ns-3 glue
converts by reinterpreting the 32 bits (`model/swarm-ns3-ports.cc`, lines 14
to 26): an IPv4 address becomes the number `Ipv4Address::Get()` returns,
0.0.0.0 becomes `kInvalidAddress`, and 255.255.255.255 reads as -1 by its
bits; back, `kInvalidAddress` becomes 0.0.0.0 ("Neither is ever a node's
address, so the round trip is exact for every real node",
`model/swarm-ns3-ports.h`).

So a node's identity is one IPv4 address. AntHocNet ADR-0011 chose that on
purpose ("`NodeAddress` is the node's primary AntHocNet-interface IPv4
address […] the core address space *is* the IP space") and rejected an
abstract node id because "it forces an adapter-maintained id↔IP directory
[…] that AntHocNet does not otherwise need and would have to populate and
keep fresh". swarm-routing/AntHocNet#218 then recorded what the choice
leaves open on a node with several interfaces, in three points:

1. the map from a node's name to the link it is reached over is learned
   from hello, so a peer whose hello has not arrived, or is suppressed, has
   no entry;
2. a node addressed by an address other than its first interface's is a
   second destination to the core, discovered and stored separately;
3. when the first interface goes down, the name goes with it, while the
   node is still reachable over its other links.

Two things in the issue's text are no longer what `main` says:

- "`SwarmRoutingBase` has the same defect without AntHocNet's workaround."
  The workaround is now in the base: the peer map (`SetPeerRoute`,
  `ClearPeerRoute`, `ResolveNextHop`; `model/swarm-routing-base.h`, lines
  347 to 369 and 424 to 438; swarm-routing/ns3-swarm-core#127), and
  AntHocNet's adapter fills it from its hello
  (`ns3/anthocnet/model/anthocnet-routing-protocol.cc`, line 1336). Point 1
  therefore holds for every protocol on the base, not only AntHocNet.
- `MainAddress()` is still "the first socket's address"
  (`model/swarm-routing-base.cc`, lines 354 to 356), and
  `NotifyInterfaceDown` closes and erases that interface's sockets (lines
  711 to 725). So point 3 is sharper than the issue says: after the first
  interface goes down, `MainAddress()` returns another interface's address,
  while a core built with the old one keeps it (Termite's adapter builds
  its router with `ToNodeAddress(MainAddress())` and calls `MainAddress()`
  again for each packet it originates:
  `ns3/model/termite-routing-protocol.cc`, lines 60 and 146).

A host that is not ns-3 makes the question unavoidable. The daemon of
swarm-routing/.github#256 has interfaces that come and go; the data-plane
ADR puts its control and data on IPv6 link-local addresses, which are not
node names at all; and its applications may use IPv6, whose addresses are
16 bytes.

Every protocol's wire format carries node addresses as 4 bytes,
little-endian: AntHocNet's ant (`docs/wire-format.md`: `src`, `dst`, each
visited hop), ARA's ant (`core/ara-ant.h`: source, destination), BeeAdHoc's
trailer and scout (`core/beeadhoc-trailer.h`: "n x i32 route"), Termite's
header (`core/termite-packet.h`: source, destination, previous hop, next
hop) and HOPNET's data header (`core/hopnet-data.h`: source, destination,
target and each route node). Changing the width is a wire-version bump in
each (org rule 3).

Constraints this ADR stays inside: swarm-core 1.x keeps `NodeAddress` as
v1.0.0 ships it and this decision is part of 2.0.0 (`docs/api-policy.md`,
lines 206 to 208; swarm-routing/.github#253, decision of 2026-11-12); ns-3
is IPv4 today and whether it gets an IPv6 adapter is
swarm-routing/ns3-swarm-core#191, which depends on this; no protocol's
behaviour changes; security is a default-off profile (AntHocNet ADR-0020).

## Decision

1. **A `NodeAddress` is a node id: a 32-bit value that names one node, not
   one of its interfaces and not an IP address. A core never interprets
   it.** A core may compare two ids for equality, compare an id with
   `kInvalidAddress`, and order ids as the signed integers they are when it
   needs a deterministic tie-break (ARA's "ties: the lowest address",
   `core/ara-router.h`). Nothing else. The type keeps its name and its
   width; the comment that ties it to IPv4 goes.
   *Reason:* this is option (b) of the issue. It is what the cores already
   do: ADR-0011 itself says the core "treats it opaquely and only ever
   compares `== kInvalidAddress`". Only the hosts gave the value a meaning.
   Keeping the name and width means no consumer's source changes for this
   decision, and the order ARA uses stays the one ns-3 runs today, so ns-3
   output is byte-identical.

2. **The ids are every 32-bit value except `kInvalidAddress` (-1, all bits
   set), which is never a node's id on any host. A host whose rule maps ids
   to IPv4 addresses (ns-3, the daemon) also never assigns 0. The daemon
   also never assigns the subnet-broadcast address of an overlay IPv4 prefix
   (all host bits set, such as 10.1.0.255 in 10.1.0.0/24) and rejects a
   configured address of that kind, because ADR-0003 drops overlay packets
   sent to a broadcast address.** "Invalid" is
   then outside the id space by definition, which is what the issue's title
   asks for.
   *Reason:* -1 is already the sentinel in every core and on every wire
   (Termite's and HOPNET's headers default their fields to it); moving it
   would change bytes. 0 is a host matter, not the core's: it would be the
   address 0.0.0.0, which is no node's address and which the ns-3 glue maps
   to the sentinel and back, so 0 does not round-trip there. The core does
   not reserve it, because the cores' own test networks and swarm-core's
   testbench number their nodes from 0 ("a node's NodeAddress is its
   index", `core/testbench/swarm-testbench.h`) and nothing is gained by
   breaking them. A scene meant to run on every host uses ids other than 0.
   The rule is checkable: the test the issue asks for ("the full id range
   round-tripping") covers every value but -1 in `core/`, and every value
   but -1 and 0 through the ns-3 conversion.

3. **Nothing on any wire changes. An id is carried as it is today, 4 bytes,
   little-endian, in every message of every protocol. No wire version is
   bumped.**
   *Reason:* org rule 3 requires a bump when a field or its units change,
   and neither does: the same 4 bytes hold the same value. A wider id was
   weighed for IPv6 and rejected on airtime: if the identity were the IPv6
   address, every address in a visited path or a source route would grow
   from 4 to 16 bytes on links where airtime is the scarce resource (the
   first comment on the issue). A BeeAdHoc trailer for a 10-hop route would
   grow from 52 to 184 bytes; an AntHocNet forward ant grows by 12 bytes
   per hop travelled.

4. **The host maps the addresses applications use to ids by a rule that
   needs no table.**
   - *ns-3 (IPv4):* the id of a node is the 32 bits of its primary IPv4
     address, as the number `Ipv4Address::Get()` returns (a.b.c.d is
     a·2²⁴ + b·2¹⁶ + c·2⁸ + d). This is today's conversion, unchanged.
   - *The Linux daemon:* a node has one overlay IPv4 address, one overlay
     IPv6 address, or both. Its overlay IPv4 address is its id read as an
     IPv4 address by the same number rule. Its overlay IPv6 address is one
     96-bit prefix, the same for the whole network, followed by the id. An
     inner packet's destination address therefore gives the destination's
     id by reading 32 bits; an address outside the overlay prefixes names
     no node.
   - *The discrete-event host:* a node's id is given by the scene.
   *Reason:* this answers the objection ADR-0011 raised against an abstract
   id. There is no id-to-address directory to "populate and keep fresh" for
   destinations, because the map is a function. It also answers the second
   open point of the issue's first comment: a reactive protocol can search
   for a destination nobody has heard from, since the id is computed from
   the address; no discovery message has to carry a full address, and no
   hash is involved, so there is no collision to give a rule for. And the
   same node has the same id, and the same bytes in every control message,
   whether it is simulated on ns-3 with address 10.1.0.5 or runs as a
   daemon with overlay address 10.1.0.5 or `<prefix>::a01:5`, which is what
   swarm-routing/.github#256's byte comparison needs.

5. **The one map a host learns is where a neighbour is reached: from a
   neighbour's id to the underlay address and interface it was last heard
   on. On the daemon it is filled from every frame received, because every
   frame names its sender's id (data-plane ADR, decision 3); it is bounded
   and its entries age.** A unicast to an id with no entry fails at once and
   is reported as a send failure.
   *Reason:* this is a neighbour table, which every network stack has, not
   a directory of the network. It does not depend on any protocol having a
   periodic message: ARA and Termite, which have none, fill it with
   whatever they send. A core only unicasts to a node it received something
   from, so the entry exists when it is needed; if it was evicted, the
   failure is reported like any other.

6. **Ids are assigned, like addresses: by the scenario on ns-3 and in the
   discrete-event host, by configuration on a daemon. An id is unique in
   one network. A host that hears a frame carrying its own id from another
   node reports a duplicate and does nothing else.**
   *Reason:* uniqueness by assignment is how every IP network already
   works, and the scenes of swarm-routing/.github#268 need addresses they
   can name in advance (RT-4's "address no node has"). Deriving the id from
   something else is deferred (see "Alternatives" and open question 2).
   Detecting a duplicate is cheap and host-level; resolving one would be a
   protocol.

7. **AntHocNet ADR-0011 is superseded in its first and third decisions and
   kept in its second.** Superseded: "`NodeAddress` is the node's primary
   AntHocNet-interface IPv4 address" and "Identity ≠ interface […] its
   *primary* AntHocNet interface IP". They become a property of one host:
   on ns-3 with IPv4, a node's id is the 32 bits of its primary address
   (decision 4). Kept, and now a rule of the shared core: "Broadcast/
   multicast are a verb, never a noun", expressed only as
   `RouteAction::Broadcast`. Its rejected alternative ("abstract node id")
   is the one adopted here, with the objection answered by decisions 4 and
   5. ADR-0011 gets a status line pointing to this ADR; its text is not
   edited.

8. **swarm-routing/AntHocNet#218's three points, answered per host.**

   | Point | On the Linux daemon | On ns-3 |
   |---|---|---|
   | 1. The map depends on hello | No longer: the neighbour map is filled from every frame (decision 5), so suppressing or lacking hellos does not empty it | **The workaround stays and is documented here.** A neighbour named by an id that is on no subnet of the receiving node is reached through the peer map, which a protocol fills from the one message that carries both names (AntHocNet's hello). A protocol that suppresses that message on a link must fill the map another way (the base's `OnLinkUp` from link state, swarm-routing/ns3-swarm-core#4). Single-interface scenes never use the map |
   | 2. A second address is a second destination | Cannot happen: a node has one id, its overlay addresses map to it by rule, and no underlay address is ever a destination | **Stays, as a documented limit.** On ns-3 a node is addressed by its primary address. Traffic sent to another of its addresses is a destination the cores discover separately; correctness holds, state is duplicated. No scene and no family addresses a node that way |
   | 3. The first interface down takes the identity | Cannot happen: the id belongs to no interface | **Fixed by a rule:** the glue reads the node's id once, when the protocol starts, and keeps it for the node's lifetime, whatever its interfaces then do. It is a value, not the state of an interface. Today `MainAddress()` changes when the first interface goes down; the change to make it stable is part of this issue's implementation and alters output only for a node with several interfaces whose first one goes down |

9. **ns-3 stays IPv4, unchanged, and byte-identical.** The glue's two
   conversion functions keep their behaviour; `core/` loses the IPv4
   comment and gains the statement of decisions 1 and 2. What
   swarm-routing/ns3-swarm-core#191 inherits: the cores and every control
   message carry no address family, so an IPv6 run on a daemon and the IPv4
   reference run of the same scene on ns-3 exchange the same control bytes
   when the nodes have the same ids. This ADR does not decide #191; it
   removes the reason an IPv6 adapter on ns-3 would be needed for the
   *cores'* sake, and leaves the glue's sake to that issue.

10. **Identity does not depend on the security work.** A node has an id with
    security off, which is the default (AntHocNet ADR-0020). An
    authenticated profile may later bind a key to an id; how is
    swarm-routing/AntHocNet#1078's design, and decision 4 leaves room for it
    (see "Alternatives", the key-derived address).

## Consequences

- **For swarm-core 2.0.0** (the issue's second and third criteria): `core/`
  has no comment or constant tying the id to IPv4; tests cover the id range
  round-tripping as decision 2 states it,
  and a host with two interfaces mapping to one id; every consumer builds
  with no source change and byte-identical output, since neither the type
  nor a wire width moves. The `release:` label is 2.0.0's.
- **The router observer's trace sources on ns-3 keep printing IPv4
  addresses** (`RouteChanged`, `DataDecision`; `model/swarm-routing-base.h`),
  by the same conversion. Off ns-3 the same events print ids; the
  measurement library of swarm-routing/.github#270 prints an id the way the
  ns-3 harness prints the address it equals, so a reader sees the same
  text.
- **An id names a node, never a network.** A destination outside the
  overlay prefixes has no id, so a daemon cannot route to a gateway's far
  side. That is a limit of the model, accepted for now: no scene, family or
  fitness test asks for it.
- **A daemon network needs an assignment of ids**, as it would need an
  assignment of addresses. The packages of swarm-routing/.github#267 have
  to ask for one or propose one (open question 2).
- **The 32-bit space is the size limit of one network**, about four billion
  nodes, which is not a limit in practice. A protocol of the org's own
  (swarm-routing/.github#269, section 5: a fork "inherits […] its 32-bit
  node ids") may choose otherwise in its first wire version; this ADR binds
  the five existing protocols.
- **One neighbour, one underlay address.** The neighbour map keeps the
  address a neighbour was last heard on. Two nodes joined by two radios use
  whichever was heard last; choosing between parallel links is not decided
  here (the base's peer map has the same rule: "One entry per canonical
  address").
- **Ruled out:** a core that reads an id as an address or a prefix; a wire
  field wider than 4 bytes for a node in the five protocols; a host that
  learns the id of a destination from the network.

## Alternatives considered

- **(a) Keep IP-as-identity and document the workaround.** Rejected for the
  shared core: the daemon's underlay addresses are link-local and say
  nothing about the node, an IPv6 identity would quadruple every address on
  the wire, and the three points of swarm-routing/AntHocNet#218 would stay
  open on the host where interfaces really come and go. It is, however,
  exactly what decision 4 keeps *on ns-3*, where it costs nothing; the
  workaround for points 1 and 2 there is documented in decision 8, as
  swarm-routing/AntHocNet#218 asks ("say so explicitly").
- **(c) A node advertises all its addresses.** Rejected: it grows a hello
  with the node's degree ("the wrong direction at constellation scale",
  swarm-routing/AntHocNet#218), three of the five protocols have no hello
  to carry it (swarm-routing/ns3-swarm-core#73), and it is a wire change in
  each protocol that has one.
- **A wider id on the wire** (8 or 16 bytes). Rejected: a wire-version bump
  in five protocols and more airtime, for a space nobody needs (decision
  3).
- **An id derived by hashing an address or an interface identifier.**
  Rejected: a truncated hash is not unique by construction, so it needs a
  collision rule, and a collision is two nodes with one identity. An
  assigned id needs no rule. (With n nodes and 32 bits the chance of some
  collision is about n²/2³³: negligible at 100 nodes, about 1 % at 10 000.)
- **An address derived from the node's signing key**, in the manner of
  cryptographically generated addresses
  ([RFC 3972](https://www.rfc-editor.org/rfc/rfc3972)), with the id as the
  low 32 bits (the issue's second comment). Deferred, not rejected, and the
  four points that comment asks to be weighed are answered so:
  1. *32 bits is not a binding.* Agreed: about four billion tries find a
     key for a chosen 32-bit id. If the scheme is ever adopted, what a
     neighbour verifies is the full overlay address against the key, and
     the id is only the index the cores use. That check belongs to the
     authenticated profile, not to the id.
  2. *It only covers overlay addresses.* Agreed, and decision 5 already
     covers the rest: the underlay address of a neighbour is learned from
     its frames, key or no key. An IPv4 overlay address has no room for a
     hash; under decision 4 it does not need one.
  3. *It makes identity depend on the security work.* This is the reason to
     defer: decision 10. With security off a node would need a key only to
     have an address.
  4. *ns-3 has none of it.* Agreed; scenarios assign addresses there.
  The scheme stays possible later without touching this ADR's decisions:
  decision 4 only requires that the id is the low 32 bits of the overlay
  IPv6 address. A key-derived address needs a prefix shorter than 96 bits
  and a collision rule for the low 32 bits; both would be a new ADR, taken
  with swarm-routing/AntHocNet#1078.
- **Renaming the type in 2.0.0** (to a name without "Address", or to a
  distinct type the compiler checks): open question 1.

## Questions decided with this ADR

Each question below was put to the maintainer with a recommendation; on
2026-11-16 every recommendation was accepted, so each "Recommendation" is
the decision.

1. **Rename `NodeAddress`, or make it a distinct type, in 2.0.0?**
   Recommendation: no. Keep the alias and its name; change only its
   documentation.
   Reason: the name appears in every core's signatures and tests in six
   repositories; a rename is a large diff with no change of behaviour, and
   a distinct type would touch every wire codec that writes it as an
   integer. 2.0.0 already carries three contract changes
   (swarm-routing/ns3-swarm-core#71, #73 and the host library). If a
   distinct type is wanted later, it is one mechanical change of its own.
2. **How does a daemon get its id?**
   Recommendation: from its configuration, required, with no default; the
   package's set-up step may *propose* one (for example from the low bits
   of the board's hardware address) for the user to confirm, and the
   duplicate report of decision 6 is the safety net.
   Reason: a silent default is a silent collision. The scenes need ids
   known in advance in any case, and the testbed of
   swarm-routing/.github#259 is provisioned by a runner that can write
   them.
3. **Which overlay prefixes?**
   Recommendation: none fixed in the daemon. The network's configuration
   names an IPv4 prefix the ids fall in, a 96-bit IPv6 prefix, or both; the
   scenes use the addresses the ns-3 reference run uses (so ids match) and
   a unique-local IPv6 prefix written in the scene file.
   Reason: a built-in prefix is a per-network preset by another name, and
   matching the ns-3 addresses is what makes the control bytes comparable.
4. **Does the ns-3 glue get the stable-id rule (decision 8, point 3) in
   1.x or in 2.0.0?**
   Recommendation: 2.0.0, with the rest of this issue.
   Reason: it changes what `MainAddress()` returns in one case, which is a
   documented behaviour of a frozen API (`docs/api-policy.md`), and no
   scene in use takes a node's first interface down.
5. **What should swarm-routing/ns3-swarm-core#191 conclude?** Not this
   ADR's decision, but it follows closely.
   Recommendation: its option (a), no IPv6 adapter on ns-3 for now; an IPv6
   daemon run is judged against the IPv4 reference of the same scene with
   the same ids.
   Reason: decision 9. The control bytes are the same; only the carrier
   differs, and the carrier on the daemon is the underlay, which ns-3 does
   not model in either family.

## Not verified

- That no core unicasts to a node it has not received from (decision 5's
  reason) was reasoned from each core's entry points (`from` is always the
  sender of a received message; BeeAdHoc's and HOPNET's source routes were
  walked by a scout or an ant in both directions), not tested. The
  discrete-event host can assert it.
- The collision estimate is the birthday approximation, not a measurement.
- swarm-routing/AntHocNet#1078 and swarm-routing/.github#273 were not read
  for this ADR; they are cited as the issue's comments cite them.
