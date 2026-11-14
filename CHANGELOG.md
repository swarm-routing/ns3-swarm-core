## v0.3.0 (2026-11-14)

### Feat

- **core**: core/ installs as the CMake package swarm-core, found with find_package (#137)
- **core**: a discrete-event testbench for protocol cores, trend tests without a simulator (#129)
- **core**: fuzzy composite link metric, a delay x stability rule table over hop count (#130)
- **model**: learn point-to-point neighbours from link state, beacons suppressed per interface (#123)
- **core**: energy-aware link metric, AntHocNet's energy metric bit for bit (#120)
- **core**: core/ as a cmake target, a `core` check that builds it alone, and the docs at the state of main (#104)

### Fix

- **citation**: render the ppsn 2004 paper as a conference paper in citation.cff (#105)

## v0.2.0 (2026-11-13)

### Feat

- **base**: node shutdown, crash and restart on swarmroutingbase, with a helper that scripts failures for every arm (#98)
- **core**: dedup history takes an optional entry lifetime next to the count cap (#94)
- **base**: a packet the wi-fi mac gave up on comes back to the protocol, with its previous hop (#95)
- **base**: defer is protected, so an adapter overriding routeinput holds its packets (#91)

### Perf

- **model**: deferred queue without o(n²) rebuilds, cached ports, cancellable timers, a non-allocating wire path (#97)

## v0.1.0 (2026-11-11)

### Feat

- **observer**: add swarm-core's router observer, its trace sources and counters (#44)
- **model**: the ns-3 glue: swarmroutingbase, deferredpacketqueue, link monitors, helpers (#30)
- **core**: the shared simulator-free primitives — visited path, LE wire helpers, pheromone table, ports (#27)
- **ns3**: swarm-core module skeleton with every org quality gate on (#13)

### Perf

- **core**: flat per-destination pheromone rows, one-pass evaporation (#47)
