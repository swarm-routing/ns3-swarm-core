## v0.1.0 (2026-11-11)

### Feat

- **observer**: add swarm-core's router observer, its trace sources and counters (#44)
- **model**: the ns-3 glue: swarmroutingbase, deferredpacketqueue, link monitors, helpers (#30)
- **core**: the shared simulator-free primitives — visited path, LE wire helpers, pheromone table, ports (#27)
- **ns3**: swarm-core module skeleton with every org quality gate on (#13)

### Perf

- **core**: flat per-destination pheromone rows, one-pass evaporation (#47)
