# CONTEXT.md — orientation and history

Background an agent or a new contributor needs that does not belong in
`AGENTS.md`: domain notes, current state, glossary, open questions. Read the
section a task needs, not the whole file.

## Current state

**Primitives (#8).** The `swarm-core` module builds in the org's ns-3 image with
every quality gate on (strict warnings, sanitizers, coverage, determinism,
format, licence headers). `core/` holds the simulator-free primitives in
namespace `swarm::core`, extracted from AntHocNet's dual-licensed core: the
ports, the little-endian wire helpers, the bounded visited path, dedup history
and pheromone table, roulette selection, evaporation, `RouteDecision` and the
`ILinkMetric` seam. `model/` still holds only `kSwarmCoreApiVersion` and the
seed helper. Next, and the org's critical path: the ns-3 glue in `model/` (#9);
ARA, BeeAdHoc and Termite build on both. Plan and status: epic #6, handoff #18.

## Glossary

- **Port**: an interface a protocol's core calls and an adapter implements
  (`core/swarm-ports.h`): time, randomness, neighbours, timers, link signals.
  The core never reaches a simulator any other way (golden rule 10).
- **Fail closed**: `WireReader`'s behaviour on bad input: the first short read,
  wrong version, bad bool or oversized count marks it failed, and every later
  read returns 0 without moving.
- **Roulette selection**: pick a next hop with probability proportional to
  (pheromone / best)^beta, one `IRng::uniform()` draw per pick.

## Open questions

- **Which primitives count as shared** (golden rule 11): #8 took the ports, wire
  helpers, visited path, dedup history, pheromone table, roulette, evaporation,
  `RouteDecision` and `ILinkMetric`. AntHocNet's `GenerationTracker`, its
  regular/virtual table split, reinforcement formula, `IKinematics`,
  `IRouterObserver` and concrete metrics stayed behind; the maintainer confirms
  the list.
