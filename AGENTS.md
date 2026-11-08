# AGENTS.md — guide for AI coding agents

Operational guide for LLM agents working in `swarm-routing/ns3-swarm-core`. Read this
before changing anything. Org-wide conventions:
[`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md). Shape and rules mirror
[AntHocNet's AGENTS.md](https://github.com/danieljoppi/AntHocNet/blob/main/AGENTS.md), the source repository.

## What this repo is

**Status: skeleton (#7).** CI runs every org quality gate on the module; the primitives arrive in `core/` with #8 and the ns-3 glue in `model/` with #9. Directories marked (planned) do not exist yet: update this file in the PR that creates each.

The shared ns-3 contrib module `swarm-core` (`libns3.<ver>-swarm-core`) every
swarm-routing protocol links against: simulator-agnostic C++20 primitives
(`VisitedPath`, little-endian wire helpers, `PheromoneTable`, `RouteDecision`,
`ILinkState`, `ILinkMetric`, evaporation and roulette selection) extracted from
AntHocNet's dual-licensed `core/`, plus ns-3 cross-layer glue
(`SwarmRoutingBase`, `DeferredPacketQueue`, `WifiLinkMonitor`,
`EnergyMetricHook` over `ns3::EnergySource`, `IslLinkMonitor`).

```
CMakeLists.txt      build_lib(LIBNAME swarm-core ...); ns-3 generates ns3/swarm-core-module.h
core/               (planned, #8) simulator-free primitives — MIT OR GPL-2.0-only, carries core/LICENSE
model/              ns-3 glue — GPL-2.0-only (today: kSwarmCoreApiVersion, SwarmCoreSeed)
helper/             (planned) ns-3 helpers
test/               swarm-core test suite (roundtrip, fuzz, primitives, glue)
examples/           swarm-core-seed: the determinism gate's program until the primitives give it more
.github/workflows/  ci.yml (reusable-ns3-ci: strict warnings, coverage, determinism, format, headers), pr-title.yml
```

## Build & verify

CI is [`reusable-ns3-ci.yml`](https://github.com/swarm-routing/.github/blob/main/.github/workflows/reusable-ns3-ci.yml):
the module builds inside `ghcr.io/swarm-routing/ns3-dev:latest-{debug,sanitized}`
(warm ns-3 tree; only the module compiles) and runs its test.py suites.
`latest` is the release ns-3 supports (it maintains its latest only), so
automatic CI follows ns-3 with no edit; an older ns-3 release runs only on
demand, through a manual `workflow_dispatch` passing `ns3-versions`, a JSON list such as `["3.48"]` (org
CONTRIBUTING invariant 7). The
same locally:

```bash
docker run --rm -v "$PWD:/src/swarm-core:ro" ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --strict-warnings --test swarm-core=/src/swarm-core
```

Automatic runs (push, PR: `latest`) must pass `test.py -s swarm-core` on debug
and sanitized, with strict warnings as errors (our code only; ns-3's headers
are exempt), and report coverage (report-only). A manual run on older releases
(`ns3-versions`) uses its `profiles` input (default debug) without strict
warnings or coverage, since older images may predate those options. Every run
has the determinism gate: `swarm-core-seed --seed=1` twice
must be byte-identical and `--seed=2` must differ (org invariant 5). Format:
`git ls-files '*.h' '*.cc' | xargs clang-format-18 --dry-run -Werror` with the org
`.clang-format`. The suite
needs ns-3.42+ (scoped `TestCase::Duration`).

## Golden rules (invariants — do not break)

Org-wide (1–9), then this repo's own.

1. **Licensing per directory, as AntHocNet** ([`LICENSING.md`](LICENSING.md);
   AntHocNet [LICENSING.md](https://github.com/danieljoppi/AntHocNet/blob/main/LICENSING.md),
   [ADR-0028](https://github.com/danieljoppi/AntHocNet/blob/main/docs/adr/0028-core-is-dual-licensed.md)). A
   simulator-free `core/` and the standalone tooling are `MIT OR GPL-2.0-only`;
   ns-3 modules, the site, docs and data are `GPL-2.0-only`. Every original
   `.h`/`.hpp`/`.cc`/`.cpp`/`.py`/`.sh` starts with
   `// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)` +
   `// SPDX-License-Identifier: <MIT OR GPL-2.0-only | GPL-2.0-only>` (`#`
   comments where needed), matching its directory in `LICENSING.md`. CI
   (`reusable-licence-check.yml`) checks it. A new directory gets its row in
   `LICENSING.md` in the same PR.
2. **The licence boundary only goes one way.** GPL code may use the dual
   parts; dual code never pulls GPL-only code in: a `core/` file includes only
   the C++ standard library and other `core/` files (CI checks it), and a dual
   script imports no GPL-only code. Code coming from AntHocNet keeps its
   licence: its `core/`/`tools/` (dual) may land in dual or GPL paths here; its
   `ns3/` modules and `web/` (GPL-2.0-only) only in GPL paths. Third-party code
   (AntHocNet's vendored `aomdv`/`gpsr`, IITP RAS) lands only in GPL paths with
   its provenance recorded. Write clean implementations against public ns-3
   interfaces rather than copying ns-3's `src/aodv`/`src/olsr`.
3. **Wire formats are POD + versioned.** Control headers are
   simulator-independent POD structs with bounds-checked little-endian
   readers/writers, a leading version byte, and deterministic roundtrip + fuzz
   tests. Changing a field or its units bumps the version byte.
4. **No contrib module named `core`** (collides with ns-3's `src/core`). The
   shared module is `swarm-core` (`#include "ns3/swarm-core-module.h"`).
5. **Determinism.** A fixed `--seed=<N>` gives byte-identical output. All
   randomness through the simulator's seeded streams or an injected RNG port;
   never libc `rand()`, wall-clock time, or unordered-container iteration order
   in anything that reaches an output.
6. **Comparisons are paired.** Per-seed deltas across >= 20 seeds, emitting the
   canonical `##CONFIG##`, `##RUN##`, `##BENCH##`, `##DIAG##` lines. A
   comparator arm (a protocol we did not design) counts only after its three
   gates: a fidelity sheet from its paper, the per-PR delivery smoke, and an
   anchor reproducing the paper's own trend.
7. **Keep bounded structures bounded** (visited paths, dedup histories, queues
   carry a configured cap).
8. **CI hygiene.** Actions pinned by full commit SHA with a `# vX.Y.Z` comment;
   buildx caches scoped per profile; transient artifacts `retention-days` <= 7.
9. **Nothing shipped depends on `.claude/`.** Skills hold procedures; scripts
   that anything shipped needs live in `tools/` (AntHocNet ADR-0014).
10. **`core/` includes no simulator header.** The primitives take time, randomness, neighbours and deferred work through injected ports (as AntHocNet's `core/ports.h`), so they stay reusable, unit-testable and MIT-optional; only `model/` (GPL) includes `ns3/*`.
11. **No protocol logic here.** swarm-core holds what two or more protocols share. A protocol's decision rules live in its own repo; promote code here only when a second protocol needs it.
12. **Downstream API stability.** ARA, BeeAdHoc and later protocols build against `main`: a breaking API change lands with the downstream fixes or an issue in each consumer.
13. **Every primitive change carries a test** in `test/` (AntHocNet golden rule 7).

## Conventions

- Issues follow the org taxonomy ([CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues), from
  [AntHocNet ADR-0013](https://github.com/danieljoppi/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md)): one type label
  (`bug`/`enhancement`/`chore`/`documentation`/`verification`, plus `epic` for
  umbrellas), area label(s) (`protocol`/`adapter`/`ns3`/`benchmark`/
  `observability`/`packaging`/`learn-site`/`literature`), one `model:*`
  recommendation, one `priority:P1|P2|P3` on non-epic issues, a
  `release:vX.Y.Z` label only when a roadmap gates the issue. Labels come from
  `labels.json` via the `sync-labels` workflow.
- Issue bodies: **Context** (evidence, numbers, links) · **Scope** ·
  **Acceptance criteria** (checkboxes) · **Depends on** (cross-repo form
  `swarm-routing/<repo>#N`) · **References**. Children of an epic say "Part of
  #N" and are linked as sub-issues. Comments you post end with the Claude Code
  footer.
- Issues migrated from AntHocNet carry `migrated` and start with *Migrated from
  `danieljoppi/AntHocNet#N`*.
- Epics are listed where contributors look. In a public repository, this
  README's `## Epics` section and the org
  [ROADMAP](https://github.com/swarm-routing/.github/blob/main/ROADMAP.md#epics)
  are generated from the issues labelled `epic` by `swarm-routing/.github`'s
  `tools/epics/epics.py`, which reads each status (Planned, In progress, Done)
  and count from the sub-issues. Whenever you open, close or re-scope an epic,
  or close, reopen, add or unlink one of its sub-issues, refresh them in that
  PR or the next one, with `swarm-routing/.github` checked out next to this
  repository: `python3 ../.github/tools/epics/epics.py readme .` here, then
  `python3 tools/epics/epics.py roadmap ROADMAP.md` and
  `python3 tools/epics/epics.py profile profile/README.md` in `.github`. Never
  edit a generated section by hand; the weekly `epics` workflow in `.github`
  only reports drift. Private repositories (such as `papers`) are never listed
  and keep no generated section.
- Make minimal, reviewable changes; update the relevant doc/ADR when you change
  a documented decision.

## Git workflow

- Branch from `main` (`claude/<short-topic>`), one focused change per PR.
- **Always open a PR to `main`** once the branch is pushed — every change is
  tracked through one.
- **PRs are squash-merged** (the only merge method enabled). The PR title is
  the commit on `main`, so it is a lowercase Conventional Commit
  (`feat:`/`fix:`/`docs:`/`chore:`/`ci:`…, optional scope).
- Push with `git push -u origin <branch>`; merge when CI is green.

## Where to look

| You want to… | Go to |
|--------------|-------|
| Source of the primitives | AntHocNet `core/include/anthocnet/core/`, `core/src/` (MIT OR GPL-2.0-only → `core/` here) |
| Wire-format discipline | AntHocNet `docs/wire-format.md`, ADR-0006 |
| Link-metric seam history | AntHocNet #142 (closed), #145, #146 |
| The org's invariants, labels, issue shape | [`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md) |
| The reference implementation and its history | [`danieljoppi/AntHocNet`](https://github.com/danieljoppi/AntHocNet) (`AGENTS.md`, `CONTEXT.md`, `docs/adr/`) |
| Pick up open work | this repo's issues — highest open `priority:P*` first; newest open `handoff` issue for work in flight |
