# Architecture decision records

One file per decision, numbered in order, never renumbered. A decision that
changes is superseded by a new ADR, not edited. Start from
[`0000-template.md`](0000-template.md).

| ADR | Decision |
|-----|----------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions |
| [0002](0002-node-address-is-an-opaque-node-id.md) | A node address is an opaque 32-bit node id; the host maps addresses to it by a rule, and no wire format changes |
| [0003](0003-forward-data-in-user-space-off-ns3.md) | Off ns-3, forward data in user space through an overlay device and a link-local underlay; a kernel route is an optional fast path |
