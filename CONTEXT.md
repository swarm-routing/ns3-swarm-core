# CONTEXT.md — orientation and history

Background an agent or a new contributor needs that does not belong in
`AGENTS.md`: domain notes, current state, glossary, open questions. Read the
section a task needs, not the whole file.

## Current state

**Skeleton (#7).** The `swarm-core` module builds in the org's ns-3 image with
every quality gate on (strict warnings, sanitizers, coverage, determinism,
format, licence headers); its code today is `kSwarmCoreApiVersion` and the
seed helper. Next, and the org's critical path: the simulator-free primitives
in `core/` (#8), then the ns-3 glue in `model/` (#9); ARA, BeeAdHoc and
Termite build on them. Plan and status: epic #6, handoff #18.

## Glossary

## Open questions
