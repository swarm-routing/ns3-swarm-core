# Who uses what

swarm-core holds only what two or more protocols share (golden rule 11 in
[AGENTS.md](../AGENTS.md)). This page records which protocol uses which piece,
so a piece with fewer than two users is visible before v1.0.0 freezes the API
([api-policy.md](api-policy.md), #83).

**As of 2026-11-15**, read from each protocol repository's `main` (`core/` and
`ns3/`; tests and benchmarks not counted). AntHocNet is being ported onto
swarm-core step by step (swarm-routing/AntHocNet#1019, #14): a step name (S4,
S5, …) marks a piece it adopts in that step; until then it keeps its own copy.
Update this table in the PR that changes a piece's users.

| Piece | What it is for | ARA | BeeAdHoc | Termite | HOPNET | AntHocNet |
|---|---|---|---|---|---|---|
| `RouteDecision` (`core/swarm-route-decision.h`) | The contract between a protocol's core and its adapter: the core returns decisions (forward, hold, drop, send an ant), the adapter only carries them out | ✓ | ✓ | ✓ | ✓ | ✓ (S3) |
| Ports: `IClock`, `IRng`, `ITimerScheduler`, `INeighborProvider` (`core/swarm-ports.h`) | Time, randomness, timers and neighbours without a simulator, which keeps every core deterministic and testable alone | ✓ | ✓ | ✓ | ✓ | ✓ (S2) |
| `IRouterObserver` (`core/swarm-observer.h`) | The same internal metrics from every protocol: control traffic by kind, route changes, data decisions, state size | ✓ | ✓ | ✓ | ✓ | ✓ (S2) |
| `WireWriter`, `WireReader` (`core/swarm-wire.h`) | Versioned, bounds-checked little-endian headers (org golden rule 3) | ✓ | ✓ | ✓ | ✓ | S6 |
| `DedupHistory` (`core/swarm-dedup-history.h`) | Bounded duplicate suppression, optional entry lifetime | ✓ | ✓ | ✓ | ✓ | S4 |
| `PheromoneTable` (`core/swarm-pheromone-table.h`) | Per-destination pheromone rows, best entry, evaporation with ages (#124) | ✓ | – | ✓ | ✓ | S7 |
| Roulette selection, `evaporationFactor` (`core/swarm-selection.h`) | Probabilistic next-hop choice and decay | ✓ | – | ✓ | ✓ | S7 |
| `VisitedPath` (`core/swarm-visited-path.h`) | The bounded path an ant carries, with back-times (#125) | – | – | – | ✓ | S5 |
| `EnergyMetric` (`core/swarm-energy-metric.h`) | Battery-aware link cost (1 / residual energy per hop) | – | ✓ | – | – | S8 |
| `FuzzyMetric` (`core/swarm-fuzzy-metric.h`, #3) | Fuzzy delay × stability link quality | – | – | – | – | experimental now; second user decided after 1.0.0 |
| `SwarmRoutingBase` (`model/swarm-routing-base.h`) | ns-3 adapter base: sockets, held packets, previous hop, node lifecycle, observer trace sources | ✓ | ✓ | ✓ | ✓ | S10 |
| `DeferredPacketQueue` (`model/swarm-deferred-packet-queue.h`) | Packets waiting for a route, with hold classes (#126) | ✓ | through the base | through the base | through the base | S10 |
| `WifiLinkMonitor` (`model/swarm-wifi-link-monitor.h`) | Link breaks from Wi-Fi retry-limit drops, `OnTxFailure` (#128) | ✓ | ✓ | ✓ | ✓ | S10 |
| `EnergyMetricHook` (`model/swarm-energy-metric-hook.h`) | `IEnergy` from an ns-3 `EnergySource` | – | ✓ | – | – | S10 |
| `IslLinkMonitor` (`model/swarm-isl-link-monitor.h`, #4) | Inter-satellite links without hello | – | – | – | – | experimental now; second user decided after 1.0.0 |
| `SwarmLifecycleHelper` (`helper/swarm-lifecycle-helper.h`, #43) | Scripted shutdown, crash and restart, the same for every arm | benchmarks | benchmarks | benchmarks | benchmarks | benchmarks |
| Testbench (`core/testbench/swarm-testbench.h`, #15) | A protocol core's trends tested without ns-3 | tests | tests | tests | tests | – |

"through the base" means the protocol uses the piece only through
`SwarmRoutingBase`, which owns one.

## Pieces without two users

Counting rule: a protocol counts as a user when its `core/` or `ns3/` uses
the piece. The test and benchmark tools are the exception: the testbench and
`SwarmLifecycleHelper` exist for tests and benchmarks, so their users are
counted from those (four protocols each, the rows above), and they are not
below two.

`FuzzyMetric` and `IslLinkMonitor` (with `SwarmRoutingBase`'s
`IslHelloSuppression`, `OnLinkUp`, `BeaconSuppressed` and
`IsLinkStateNeighbor`) have no protocol user yet. They are **experimental**,
outside the 1.0 SemVer promise ([api-policy.md](api-policy.md)
"Experimental", #160, maintainer decision 2026-11-15): kept, not deprecated,
and whether a second protocol adopts each is decided in a release after
1.0.0. AntHocNet has no fuzzy metric to port; its inline ISL code is where
`IslLinkMonitor` came from (#3, #4).

`EnergyMetric`, `EnergyMetricHook` and `VisitedPath` have one user until
AntHocNet's port adopts them (S8, S10, S5 of swarm-routing/AntHocNet#1019).
Any other piece still below two users when v1.0.0 is cut is made
experimental the same way, before the cut. Deprecating a piece is a later
step, in a minor release after 1.0.0, by the two-step rule in
[api-policy.md](api-policy.md).
