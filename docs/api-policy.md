# API and deprecation policy

What swarm-core promises its consumers from v1.0.0 on, how that promise is
versioned, and how an API is retired. It is the written policy the v1.0.0
row of the [roadmap](roadmap.md) asks for (#83). Until v1.0.0 is released,
the 0.x rules at the end apply.

The consumers today are the protocol repositories that build on swarm-core
(swarm-routing/ARA, swarm-routing/BeeAdHoc, swarm-routing/Termite,
swarm-routing/HOPNET; swarm-routing/AntHocNet once #14 lands) and
swarm-routing/ns3-tools' harness. Golden rule 12 of [AGENTS.md](../AGENTS.md)
("downstream API stability") is the rule this page turns into versions.

## What the API is

The API is everything a consumer can name or observe without reading
swarm-core's sources:

| Part | What is covered |
|---|---|
| `core/` public headers | Every header in `core/swarm-core-sources.cmake` (`SWARM_CORE_HEADERS`): the names in namespace `swarm::core`, their signatures, the ports' virtual functions, and the documented behaviour of each primitive (the comments in the header are its contract). This includes the byte layout `WireWriter` / `WireReader` produce (little-endian, the version byte first). |
| `core/testbench/` | The header in `SWARM_CORE_TESTBENCH_HEADERS` (#15), which protocol repositories' core tests include: the names in namespace `swarm::core::testbench` (`EventQueue`, `Rng`, `Topology`, `Network`, `NodePorts`, `INodeAdapter`, `NetworkConfig`, `TraceRecord`), their signatures and documented behaviour, including that one seed and script give one trace and digest. The digest's value itself is not API: a later version may order an event differently, so a consumer compares two runs of the same build, never a stored digest. |
| `core/` as a CMake project | The target `swarm_core` and its alias `swarm::core`, consumed with `add_subdirectory` or `FetchContent` with `SOURCE_SUBDIR core` (README "Use core/ from another CMake project"): as a subproject it adds that target only, with no test target and no `-Werror` on the consumer. Installed, the CMake package `swarm-core` (#133): `find_package(swarm-core CONFIG)` gives the imported target `swarm::core`, the headers under `include/swarm-core/` (`testbench/` included), and the package version is the release's, compatible within its major from 1.0 (`SameMajorVersion`; before 1.0 within its minor). The options `SWARM_CORE_TESTS`, `SWARM_CORE_SANITIZE`, `SWARM_CORE_BENCH` and `SWARM_CORE_INSTALL` keep their names and meaning. |
| `model/` public classes | Every header the ns-3 module installs (`HEADER_FILES` in `CMakeLists.txt`): the public and **protected** members of each class (an adapter derives from `SwarmRoutingBase`, so its protected hooks and services are API), every `TypeId` name, every attribute (name, value type, default and what the default means) and every trace source (name and callback signature). |
| `helper/` | `SwarmRoutingHelper`, `SwarmMonitorHelper`, `SwarmLifecycleHelper` and the structures they return (`SwarmRoutingStats`, ...). |
| The module itself | The contrib module name `swarm-core`, the umbrella header `ns3/swarm-core-module.h`, `SwarmCoreSeed` and `kSwarmCoreApiVersion` (below). |
| Default output | With every new option at its default, the same seed gives the same bytes (org invariant 5): a consumer's `##RUN##` lines do not move because swarm-core was updated, except by a bug fix, which follows the Patch rule below. |

Not API, and free to change in any release: namespace
`swarm::core::test`, `core/test/`, `core/bench/`, private members, log output
(`NS_LOG`), the file layout beyond the headers listed above, `tools/`, CI
workflows, timings and memory use (a performance change is never breaking,
but one that changes default output on purpose is, as above). The promise
is source compatibility: a consumer rebuilds against a new minor or patch
release unchanged, with deprecation warnings not treated as errors
("Deprecation" below). Binary
(ABI) compatibility is not promised; every consumer compiles swarm-core from
source.

## C++ standard

`core/` targets the C++ standard of the ns-3 release swarm-core supports:
ns-3's own `CMAKE_CXX_STANDARD_MINIMUM` (in its
`build-support/macros-and-definitions.cmake`; C++17 for ns-3.40, C++20 for
3.41–3.45, C++23 from 3.46), so **C++23 today** (maintainer decision
2026-11-14, #141). `core/CMakeLists.txt` sets `CMAKE_CXX_STANDARD 23` and gives
`swarm_core` the compile feature `cxx_std_23`, so every target that links
`swarm::core`, as a subproject, the installed package or the release tarball,
compiles at C++23 or later. A consumer needs a compiler for that standard.
The ns-3 module compiles the same sources at the standard of the ns-3 tree it
is built in, the same one.

When ns-3 raises its minimum, swarm-core follows in a **minor** release: the
standard is not part of the source API above (no name or signature changes),
but a consumer may need a newer compiler, so the PR opens an issue in each
consumer listed above before it merges and the release notes say so. The
`cxx-standard` job of `ci.yml` (`tools/checks/check-cxx-standard.py`) fails
when `core/` is below the minimum of the `ns3-dev` image CI builds in, so the
move is never missed.

## Versions

Releases follow [SemVer](https://semver.org), read against the API above:

- **Major** (`X.0.0`): any breaking change. Removing or renaming anything in
  the table; changing a signature, a type, an attribute's default or its
  meaning, a trace source's signature, a wire layout; adding a pure virtual
  function to a port or to `SwarmRoutingBase`; adding an overload that makes
  an existing call ambiguous or changes which overload it selects; changing
  default output on purpose.
- **Minor** (`1.Y.0`): additive changes. A new header, class, function,
  attribute or trace source; a new overload that leaves every existing call
  resolving as before; a new virtual function with a default
  body; a new option that is off by default and leaves default output
  byte-identical; a deprecation (below).
- **Patch** (`1.Y.Z`): fixes and documentation. A bug fix that changes
  output is still a fix, but its PR says which output moves, its changelog
  entry says so, and each consumer gets an issue before it merges.

Commit titles drive the bump (Commitizen, `.cz.toml`): `feat:` is a minor,
`fix:` a patch, and a breaking change is marked `feat!:` / `fix!:` or with a
`BREAKING CHANGE:` footer.

### `kSwarmCoreApiVersion`

`ns3::kSwarmCoreApiVersion` (`model/swarm-core.h`) is an integer a consumer
can test at compile time (`static_assert`) to fail early on an API it was not
written for.

- Before 1.0 it moved with each breaking change (1: the seed helper; 2: the
  primitives and the glue).
- At v1.0.0 it is frozen at the value of the API the release ships, set by
  the freeze PR (below) together with the test in
  `test/swarm-core-test-suite.cc` that asserts it.
- After 1.0 it changes only in a major release, by one per major. Minor and
  patch releases never change it: an additive feature is detected by the
  release a consumer pins, not by this constant.

## Deprecation

An API is retired in two steps, never one:

1. **Deprecate in a minor release.** The C++ name gets
   `[[deprecated("use <replacement>; removed in <next major>")]]`; an
   attribute or trace source is registered with ns-3's
   `TypeId::SupportLevel::DEPRECATED` and the same message. The replacement
   ships in the same release, so a consumer can move without waiting. The
   changelog lists it under the release's notes as deprecated, and the PR
   opens an issue in each consumer that uses it (found with `git grep` in
   each repository listed above), linked from the PR.
2. **Remove in a major release**, and no earlier than two minor releases
   after the deprecation shipped. The deprecation warning is the notice: a
   consumer moves within that window.

A deprecation must not break a consumer's build. The consumers compile
their own code with strict warnings as errors, so each keeps
`-Wdeprecated-declarations` a warning (`-Wno-error=deprecated-declarations`
next to its `-Werror`): the warning shows in every build, the build stays
green. The org's protocol repositories add that flag before v1.0.0 (tracked
on #83). And as with a breaking change, the PR that deprecates a name in use
comes with the consumers' moves in the same round, so in practice no
consumer carries the warning for long.

A deprecated API keeps working unchanged until it is removed: deprecation
never changes behaviour or default output.

## How consumers are protected

- **The pin.** Each protocol fetches `core/` at a commit (`SWARM_CORE_REF`)
  and its `swarm-core / pin` check fails when swarm-core's `core/` moves
  past it (swarm-routing/.github#190). A consumer that links `swarm::core`
  needs no edit for a new source file (#100), only the pin bump.
- **Fixes in the same round.** The ns-3 modules build against swarm-core
  `main`, so a breaking change cannot wait for a release to reach them. It
  merges only together with the consumer fixes: their PRs open and green
  against the change's branch, merged right after it (golden rule 12). After
  1.0 such a change also waits for the next major: it is held on a branch,
  or behind a new name with the old one deprecated, until the maintainer
  decides on that major.
- **The record.** Every deprecation and every breaking change has an issue
  in each consumer it touches, and the major's release notes link them.

## Out of scope for 1.x

The host contract, a protocol core on other simulators and on real
computers, is **v2.0.0** (epic #82; swarm-routing/.github#253). Its parts
(#71, a node-driver contract; #72, `NodeAddress` as an opaque node id; #73,
link signals as one port with fallbacks) are breaking by design and are not
part of 1.0: in 1.x, `NodeAddress` and the ports keep the form v1.0.0 ships.

## Before 1.0 (0.x)

Until v1.0.0, any minor release may break the API (SemVer's 0.x rule;
[SECURITY.md](../SECURITY.md)). The protection is golden rule 12 alone: a
breaking change lands with the downstream fixes or an issue in each
consumer, and `kSwarmCoreApiVersion` moves with it.

## Freezing (the v1.0.0 release)

The freeze is done in the release, on the maintainer's word, not before:

1. A freeze PR, merged right before the bump, sets `kSwarmCoreApiVersion`
   to the frozen value and updates its test, and sets
   `major_version_zero = false` in `.cz.toml`.
2. `release.yml` is dispatched with `mode: bump` and `increment: MAJOR`
   (swarm-routing/.github#211), which opens the `chore(release): bump
   version 0.x.y to 1.0.0` PR; merging it releases.
3. The v1.0.0 DOI is read from Zenodo and recorded in the roadmap.
