# Licensing

`swarm-routing/ns3-swarm-core` is licensed per directory, under the same rules as
[AntHocNet](https://github.com/swarm-routing/AntHocNet/blob/main/LICENSING.md), the source repository
([ADR-0028](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0028-core-is-dual-licensed.md)):

| Path | What | Licence |
|------|------|---------|
| `core/` | the simulator-free primitives and their tests, with the scratch consumer project in `core/test/consumer/` (no ns-3 header; [`core/LICENSE`](core/LICENSE)); much of it derived from AntHocNet's dual-licensed `core/`, each file naming its source | `MIT OR GPL-2.0-only` |
| `core/bench/` | dev-only microbenchmarks of the primitives (built only with `-DSWARM_CORE_BENCH=ON`, never part of the ns-3 module or a release). They link Google Benchmark (Apache-2.0), fetched at a pinned commit at build time and never vendored or redistributed; they are the one `core/` path that includes something outside the standard library and `core/`, so the licence check's include rule skips them (`header-exclude` in `ci.yml`) | `MIT OR GPL-2.0-only` |
| `tools/` | standalone scripts (checks, release, data handling) | `MIT OR GPL-2.0-only` |
| `model/`, `helper/`, `test/`, `examples/`, `CMakeLists.txt` | the `swarm-core` ns-3 contrib module | `GPL-2.0-only` |
| `docs`, `README.md`, `AGENTS.md`, `CLAUDE.md` | docs | `GPL-2.0-only` |
| everything else | | `GPL-2.0-only` |

Texts: [`LICENSE`](LICENSE) (GPL-2.0) and [`LICENSE-MIT`](LICENSE-MIT) (the
MIT option). Each source file states its licence in its
`SPDX-License-Identifier` line; CI checks it
(`swarm-routing/.github` `tools/checks/check-headers.py`).

## What this means in practice

- **The dual-licensed paths** (a simulator-free `core/`, the standalone
  tooling) may be used under MIT on their own — in another simulator, a routing
  daemon, firmware, or a closed product — keeping the copyright notice and the
  MIT text, or under GPL-2.0-only.
- **The ns-3 modules, the site, docs and data are GPL-2.0-only.** An ns-3
  module builds only inside a GPL-2.0 ns-3 tree, so an MIT option would change
  nothing for it.
- **The dependency only goes one way.** GPL code may use the dual-licensed
  parts; they never pull GPL-only code in. A `core/` file includes only the C++
  standard library and other `core/` files. CI enforces it.
- **Combined artifacts** (an ns-3 build, a published image, the site) are
  distributed under GPL-2.0; each dual-licensed file inside keeps its notice.
- **Contributions** to the dual-licensed paths are accepted under both
  licences; anywhere else under GPL-2.0-only. Code arriving from another
  project keeps its own licence and may only land where that licence fits
  (GPL-2.0-only code never in a dual path; third-party code, such as AntHocNet's
  vendored AOMDV/GPSR, only with its provenance recorded).
