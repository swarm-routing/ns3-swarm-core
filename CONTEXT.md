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
and the helpers. API version 2 (`kSwarmCoreApiVersion`).

**Added since, all on `main`.** The router observer (#42): a protocol's core
reports control packets by kind, route changes, data decisions and its state
footprint to `IRouterObserver` (`core/swarm-observer.h`), and
`SwarmRoutingBase` turns that into trace sources and the counters
`SwarmRoutingHelper::CollectStats` returns. An optional entry lifetime on
`DedupHistory`, next to its count cap (#33). The undelivered path (#40): a
data packet the Wi-Fi MAC gave up on comes back to the protocol with its
previous hop (`OnDataUndelivered`), which may send it elsewhere with
`ResendData`. Cancellable timers on `ITimerScheduler`, `FixedWireWriter` and
the wire-buffer path (`model/swarm-wire-buffer.h`: the core wire format
written straight into and read out of an ns-3 `Buffer`), and a deferred queue
without O(n²) rebuilds (#46). The node lifecycle (#43): `Shutdown`, `Crash`
and `Start` on `SwarmRoutingBase`, with `SwarmLifecycleHelper` scripting the
same failure for every arm. The energy-aware link metric (#2):
`EnergyMetric` (`core/swarm-energy-metric.h`), AntHocNet's `energy` metric
bit for bit, each relay pricing its own hop at 1 / residual energy. The fuzzy
composite link metric (#3): `FuzzyMetric` (`core/swarm-fuzzy-metric.h`), a
nine-rule delay x stability table whose quality is divided by hop count,
with `LinkObservation::stability` as its new input; every breakpoint and rule
is a repo choice.

ARA, BeeAdHoc and Termite have their ns-3 modules on `SwarmRoutingBase`
(each repository's AGENTS.md); HOPNET's is planned. The plan is
[`docs/roadmap.md`](docs/roadmap.md); epic #6, which carried it until v0.1.0,
is closed.

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
- **Router observer**: the optional `IRouterObserver` a protocol's core
  reports to (`core/swarm-observer.h`); it only listens, so a run's output is
  byte-identical with and without one. `SwarmRoutingBase::Observer()` is the
  adapter's, behind the `ControlTx`, `ControlRx`, `RouteChanged`,
  `DataDecision` and `StateSize` trace sources.
- **Lifecycle state**: a node is `Up` or `Down` (`SwarmNodeState`). A
  **shutdown** is graceful: `DoNodeShutdown` runs while the node is still up,
  so the protocol may announce it is leaving. A **crash** is abrupt: the node
  is down at once and nothing it sends leaves. `Start` brings it back and
  the protocol re-initialises (`DoNodeStart`). Each change fires the `Lifecycle` trace source.

## Open questions

- **Which primitives count as shared** (golden rule 11): #8 took the ports, wire
  helpers, visited path, dedup history, pheromone table, roulette, evaporation,
  `RouteDecision` and `ILinkMetric`; `IRouterObserver` followed, widened for
  every protocol (#42, `core/swarm-observer.h`). AntHocNet's
  `GenerationTracker`, its regular/virtual table split, reinforcement formula,
  `IKinematics` and concrete metrics stayed behind; the maintainer confirms
  the list.
