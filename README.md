# ns3-swarm-core

<!-- org-sync:begin badges -->
[![CI](https://github.com/swarm-routing/ns3-swarm-core/actions/workflows/ci.yml/badge.svg)](https://github.com/swarm-routing/ns3-swarm-core/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/swarm-routing/ns3-swarm-core?sort=semver&cacheSeconds=1800)](https://github.com/swarm-routing/ns3-swarm-core/releases)
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.24138628.svg)](https://doi.org/10.5281/zenodo.24138628)
[![Cite](https://img.shields.io/badge/cite-CITATION.cff-blue)](CITATION.cff)
[![Licence: GPL-2.0-only, core/ tools/ MIT OR GPL-2.0-only](https://img.shields.io/badge/licence-GPL--2.0--only%2C%20core%2F%20tools%2F%20MIT%20OR%20GPL--2.0--only-blue)](LICENSING.md)
[![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-yellow)](https://www.conventionalcommits.org)
[![Colony Arena: ns3-swarm-core](https://img.shields.io/badge/Colony%20Arena-project%20page-orange)](https://swarm-routing.github.io/projects/ns3-swarm-core/)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/swarm-routing/ns3-swarm-core/badge)](https://scorecard.dev/viewer/?uri=github.com/swarm-routing/ns3-swarm-core)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus)](AGENTS.md)
[![Runs on ns-3](https://img.shields.io/badge/runs%20on-ns--3-informational)](https://www.nsnam.org)
[![Built on: ns3-dev](https://img.shields.io/badge/built%20on-ns3--dev%20image-blue?logo=docker)](https://github.com/orgs/swarm-routing/packages/container/package/ns3-dev)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_ns3-swarm-core&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=swarm-routing_ns3-swarm-core)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_ns3-swarm-core&metric=bugs)](https://sonarcloud.io/summary/new_code?id=swarm-routing_ns3-swarm-core)
[![Security Rating](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_ns3-swarm-core&metric=security_rating)](https://sonarcloud.io/summary/new_code?id=swarm-routing_ns3-swarm-core)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=swarm-routing_ns3-swarm-core&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=swarm-routing_ns3-swarm-core)
[![codecov](https://codecov.io/gh/swarm-routing/ns3-swarm-core/graph/badge.svg)](https://codecov.io/gh/swarm-routing/ns3-swarm-core)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/swarm-routing/ns3-swarm-core?utm_source=oss&utm_medium=github&utm_campaign=swarm-routing%2Fns3-swarm-core&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)
<!-- org-sync:end badges -->

The shared ns-3 contrib module `swarm-core` that every swarm-routing protocol
links against: simulator-agnostic C++20 primitives (visited path, little-endian
wire helpers, pheromone table, route decision, link-state and link-metric
seams) and ns-3 cross-layer glue. **Status:** primitives in `core/`
(namespace `swarm::core`, `MIT OR GPL-2.0-only`, buildable and testable without
ns-3); the ns-3 glue is in `model/` and `helper/` (`GPL-2.0-only`):
`SwarmRoutingBase`, the `Ipv4RoutingProtocol` a protocol's adapter derives from;
`DeferredPacketQueue`; the link monitors `WifiLinkMonitor`, `IslLinkMonitor` and
`EnergyMetricHook`, which implement the core's ports; `SwarmRoutingHelper`,
which installs a protocol and its monitors in one call and collects each
node's counters (`CollectStats`); the router observer (`IRouterObserver`),
through which a protocol reports its control packets, route changes, data
decisions and state size as trace sources; and `SwarmLifecycleHelper`, which
scripts node shutdown, crash and restart the same way for every protocol. CI
runs every org quality gate.

How to work here, for people and AI agents alike: [AGENTS.md](AGENTS.md) has
the rules and how to build and verify; the release plan is [docs/roadmap.md](docs/roadmap.md); the org conventions are in
[CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).

## Use core/ from another CMake project

`core/` needs no ns-3: a C++20 compiler and CMake 3.20. Added by another
project, its `CMakeLists.txt` defines the static library `swarm_core` (alias
`swarm::core`) and nothing else: no test target, no entry in your `ctest`, and
its strict warnings stay on its own sources, never as errors. Pin a commit:

```cmake
include(FetchContent)
FetchContent_Declare(swarm_core
  GIT_REPOSITORY https://github.com/swarm-routing/ns3-swarm-core.git
  GIT_TAG <the 40-character commit you pin>
  SOURCE_SUBDIR core)
FetchContent_MakeAvailable(swarm_core)
target_link_libraries(my_protocol_core PUBLIC swarm::core)   # #include "swarm-ports.h", ...
```

With a checkout on disk, `add_subdirectory(/path/to/ns3-swarm-core/core swarm-core)`
does the same. A file added to `core/` then reaches you with the next pin, with
no edit on your side. The pin must be a commit that has
`core/swarm-core-sources.cmake` (any after v0.1.0's). `core/` is
`MIT OR GPL-2.0-only` ([LICENSING.md](LICENSING.md)); a scratch project that
does exactly this is built on every PR (`core/test/consumer/`,
`tools/checks/check-core-consumer.sh`).

## Test a protocol core without a simulator

`core/testbench/swarm-testbench.h` is a discrete-event testbench for a
protocol's core tests (#15): one event queue drives the ports a core takes
(`IClock`, `IRng`, `ITimerScheduler`, `INeighborProvider`) on N nodes, over a
scripted topology (links up and down, partitions) and an ideal MAC
(broadcast and unicast after a fixed delay, no loss unless scripted, a
unicast without a link handed back as a retry-limit failure). It lets a core
test assert a trend (local repair beats re-flooding, a storm stays bounded on
a partition, evaporation forgets a stale route) with no ns-3, so a failure
means the algorithm, not the PHY or the harness. It is header-only and
`MIT OR GPL-2.0-only`, and the same seed and script give the same trace and
digest.

A protocol repository that links `swarm::core` (above) includes it from its
core tests, with no source to list:

1. Write one adapter per protocol: derive from
   `swarm::core::testbench::INodeAdapter<Msg>`, where `Msg` is your core's
   message. Build your core from the `NodePorts<Msg>&` the factory hands you
   (`clock()`, `rng()`, `timers()`, the ports object as the neighbour
   provider). Turn its decisions into `ports.broadcast(msg)` and
   `ports.unicast(to, msg)`, and feed `receive`, `undelivered` and
   `linkChanged` to it.
2. In a test, build a `Network<Msg>` on a `Topology` (say `Topology::line(6)`),
   `install` the adapter, script the run (`linkAt`, `setLoss`, `at`), `run`,
   and assert on the counters (`sent(kind)`, `sentBy`, `deliveredTotal`) and
   on your adapters' state (`nodeAs<YourAdapter>(i)`).
3. For determinism, run twice with one seed and compare `digest()`.

The header's top comment has a 25-line adapter, and
`core/test/test-testbench.cc` has a full reference: reactive discovery on a
6-node line with one link break, where local repair costs about half the
control of re-flooding from the source and loses no data. In ARA, BeeAdHoc,
Termite and HOPNET the adapter wraps the core's router, as each ns-3 adapter
does; they link `swarm::core`, so the header is already on their include path.

## API stability

What counts as the API, which change is breaking, what `kSwarmCoreApiVersion`
means and how an API is deprecated: [docs/api-policy.md](docs/api-policy.md).
Releases are 0.x until v1.0.0, which freezes the API (#83).

<!-- org-sync:begin citing -->
## Releases & citing

Versions follow [SemVer](https://semver.org) ([CHANGELOG.md](CHANGELOG.md)). Each release is listed under [Releases](https://github.com/swarm-routing/ns3-swarm-core/releases), built by the org's release workflow and archived on [Zenodo](https://zenodo.org) with its own DOI.

To cite this repository, use the “Cite this repository” button (from [CITATION.cff](CITATION.cff)):

| DOI | resolves to |
|---|---|
| [`10.5281/zenodo.24138628`](https://doi.org/10.5281/zenodo.24138628) | **concept DOI**: always the latest release. This is the badge above, and the right citation when you mean this project. |

Each release also has its own **version DOI**, listed on [its Zenodo record](https://doi.org/10.5281/zenodo.24138628): cite that one when you reproduce a number measured at that release.

Please also cite the work it builds on:

- D. H. Joppi, *AntHocNet for NS-3*, 2026, [doi:10.5281/zenodo.20981979](https://doi.org/10.5281/zenodo.20981979). The source of the primitives in core/ and the ns-3 glue in model/: each file names its AntHocNet source and commit.
- G. Di Caro, F. Ducatelle, L. M. Gambardella, *AntHocNet: An Ant-Based Hybrid Routing Algorithm for Mobile Ad Hoc Networks*, Parallel Problem Solving from Nature - PPSN VIII, pp. 461–470, 2004, [doi:10.1007/978-3-540-30217-9_47](https://doi.org/10.1007/978-3-540-30217-9_47). The algorithm the primitives in core/ were extracted from: the pheromone table, its evaporation and the roulette next-hop selection follow it.
- G. F. Riley, T. R. Henderson, *The ns-3 Network Simulator*, Modeling and Tools for Network Simulation, pp. 15–34, 2010, [doi:10.1007/978-3-642-12331-3_2](https://doi.org/10.1007/978-3-642-12331-3_2). The simulator the glue in model/ and helper/ is written for.

Using it in your work? Contributions are welcome, and citing the DOI helps just as much.
<!-- org-sync:end citing -->

## Epics

<!-- epics:begin -->
<!-- Generated by swarm-routing/.github tools/epics/epics.py from the issues labelled `epic`; edit the issues, then re-run it. -->

What is planned and in progress here: the issues labelled [`epic`](https://github.com/swarm-routing/ns3-swarm-core/issues?q=label%3Aepic). An epic's open sub-issues carry a `priority:*` and a `model:*` label, so a contributor or an AI agent can pick one up ([how issues work](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues)). The whole org's plan: the [roadmap](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics).

| Epic | Status | Sub-issues closed |
|------|--------|-------------------|
| [#82](https://github.com/swarm-routing/ns3-swarm-core/issues/82) swarm-core 2.0.0: the host contract, so a protocol core runs on any host | Planned | 0 / 4 |

<details><summary>Closed: 1 epic</summary>

| Epic | Closed |
|------|--------|
| [#6](https://github.com/swarm-routing/ns3-swarm-core/issues/6) swarm-core: the shared contrib module every swarm protocol links (Phase 2) | Done 2026-11-11 |

</details>

<!-- epics:end -->

## Licence

See [LICENSING.md](LICENSING.md).
