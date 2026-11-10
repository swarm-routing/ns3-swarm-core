# CONTEXT.md — orientation and history

Background an agent or a new contributor needs that does not belong in
`AGENTS.md`: domain notes, current state, glossary, open questions. Read the
section a task needs, not the whole file.

## Current state

**Primitives (#8) and ns-3 glue (#9).** The `swarm-core` module builds in the
org's ns-3 image with every quality gate on (strict warnings, sanitizers,
coverage, determinism, format, licence headers). `core/` holds the
simulator-free primitives in namespace `swarm::core`, extracted from AntHocNet's
dual-licensed core: the ports, the little-endian wire helpers, the bounded
visited path, dedup history and pheromone table, roulette selection,
evaporation, `RouteDecision` and the `ILinkMetric` seam. `model/` and `helper/`
hold the ns-3 glue, taken from AntHocNet's GPL-only ns-3 modules (its adaptive
`RoutingBase`, pending queue and MAC signals): `SwarmRoutingBase`,
`DeferredPacketQueue`, `WifiLinkMonitor`, `IslLinkMonitor`, `EnergyMetricHook`
and the helpers. API version 2. Next: ARA, BeeAdHoc and Termite build their
adapters on both. Plan and status: epic #6.

**Two things the glue does differently from AntHocNet, on purpose.**
`EnergyMetricHook` never calls `EnergySource::GetRemainingEnergy()`: that call
updates the source and reschedules its update event, so reading it would change
the run; the hook listens to the `RemainingEnergy` trace and reports the value
of the last periodic update. `WifiLinkMonitor` does not count the MPDU being
acknowledged as backlog: ns-3 (3.49) fires `AckedMpdu` while that MPDU is still
in its queue, so counting it turns the idle gap after a lone frame into one huge
service-time sample (AntHocNet's `NotifyAckedMpdu` counts it).

## Glossary

- **Port**: an interface a protocol's core calls and an adapter implements
  (`core/swarm-ports.h`): time, randomness, neighbours, timers, link signals.
  The core never reaches a simulator any other way (golden rule 10).
- **Fail closed**: `WireReader`'s behaviour on bad input: the first short read,
  wrong version, bad bool or oversized count marks it failed, and every later
  read returns 0 without moving.
- **Monitor**: a node-level object (`WifiLinkMonitor`, `IslLinkMonitor`,
  `EnergyMetricHook`) aggregated to the node, reading trace sources only, that
  implements a port; `SwarmRoutingBase` hands it out, or a null port without it.
- **Deferred route**: a data packet with no next hop goes through loopback into
  the `DeferredPacketQueue` with its forwarding callbacks, and leaves when the
  protocol learns a route or is dropped through its error callback.
- **Roulette selection**: pick a next hop with probability proportional to
  (pheromone / best)^beta, one `IRng::uniform()` draw per pick.

## Open questions

- **Which primitives count as shared** (golden rule 11): #8 took the ports, wire
  helpers, visited path, dedup history, pheromone table, roulette, evaporation,
  `RouteDecision` and `ILinkMetric`. AntHocNet's `GenerationTracker`, its
  regular/virtual table split, reinforcement formula, `IKinematics`,
  `IRouterObserver` and concrete metrics stayed behind; the maintainer confirms
  the list.
