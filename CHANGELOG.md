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
