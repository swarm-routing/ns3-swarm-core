# Developer guide

ns3-swarm-core is the shared ns-3 contrib module `swarm-core` that every
swarm-routing protocol builds on. It has two halves: `core/`, plain C++23
primitives that build and test without any simulator, and the ns-3 glue in
`model/`, `helper/` and `test/`, which builds inside an ns-3 tree. This page
shows how to set up, build, test and lint both halves, and what CI checks on
a pull request. The rules a change must keep are in [AGENTS.md](../AGENTS.md)
("Golden rules"); how issues and pull requests work is in the org's
[CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).

## Prerequisites

| Tool | Version | Needed for |
| --- | --- | --- |
| Linux or macOS, `git`, `make` | any recent | everything |
| C++ compiler with C++23 | GCC 13 or Clang 17 or newer (CI: GCC 13.3 on Ubuntu 24.04) | `core/` alone |
| CMake | 3.20 or newer (CI: 3.28) | `core/` alone |
| Docker | any recent; about 4 GB free for the debug image, 6 GB more for the sanitized one | the ns-3 module, `make test` |
| Python | 3.11 or newer | the checks under `tools/`, licence headers, org-sync |
| clang-format | 18 (`clang-format-18`) | formatting |

You do not install ns-3 yourself: the module builds inside the org's
prebuilt image `ghcr.io/swarm-routing/ns3-dev` (ns-3 3.49 today, GCC 13.3,
CMake 3.28, Python 3.12), which `make` pulls on first use.

## Get the code

```bash
git clone https://github.com/swarm-routing/ns3-swarm-core.git
cd ns3-swarm-core
```

There are no submodules. To propose a change from outside the org, fork the
repository first and clone your fork (CONTRIBUTING "Outside contributions").

## Dependencies

swarm-core depends on nothing but the C++ standard library (`core/`) and
ns-3 (the glue). ns-3 comes with the `ns3-dev` image; nothing is vendored.
The microbenchmarks fetch Google Benchmark at the commit and hash pinned in
`core/CMakeLists.txt`, only when you turn them on.

Other repositories consume swarm-core through a release lock
(`swarm-core.lock`, see the README "Use core/ from another CMake project"
and CONTRIBUTING "Dependencies"). A vcpkg port for that is planned
(swarm-routing/.github#489).

## Build

`core/` alone, with the same strict warnings as CI:

```bash
cmake -S core -B build/core -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core -j
```

The ns-3 module, inside the `ns3-dev` image (your checkout is mounted
read-only; only this module compiles, against ns-3's prebuilt tree):

```bash
make build
# the same command written out:
docker run --rm -v "$PWD:/src/swarm-core:ro" ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib swarm-core=/src/swarm-core
```

`core/` also installs as the CMake package `swarm-core`:

```bash
cmake -S core -B build/core -DSWARM_CORE_TESTS=OFF && cmake --build build/core
cmake --install build/core --prefix /path/to/prefix
```

## Test

| What | Command | CI |
| --- | --- | --- |
| `core/` unit tests | `make core-test` (or `ctest --test-dir build/core --output-on-failure` after the build above) | `core` (core.yml) |
| `core/` under ASan + UBSan | `cmake -S core -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DSWARM_CORE_SANITIZE=ON && cmake --build build/sanitized -j && ctest --test-dir build/sanitized --output-on-failure` | `core` |
| `core/` used by another CMake project, installed, and from the release tarball | `tools/checks/check-core-consumer.sh build/consumer` | `core` |
| the ns-3 suite `swarm-core`, strict warnings as errors | `make test` | `ci / ns-latest debug` |
| the same on the sanitized image | `make test NS3_IMAGE=ghcr.io/swarm-routing/ns3-dev:latest-sanitized` | `ci / ns-latest sanitized` |
| the C++ standard matches the image's ns-3 | `python3 -I tools/checks/check-cxx-standard.py --ns3 /opt/ns-3 core/CMakeLists.txt` (inside the image: `make shell`) | `cxx-standard` |
| the scripts' own tests | `for t in tools/checks/tests/test_*.py; do python3 -I "$t"; done` | `python-quality` |

`make test` prints `1 of 1 tests passed` when the suite passes; the first run
pulls the image, later runs take under a minute.

CI also runs, without a local equivalent you need: the determinism gate
(`swarm-core-seed --seed=1` twice must print the same bytes and `--seed=2`
must differ), coverage for Codecov and SonarQube Cloud (report-only), and
the microbenchmarks against the base branch (report-only, a comment on the
PR).

## Run

The module ships one small program, `swarm-core-seed`, the determinism
gate's: it prints seeded random draws and a roulette sequence.

```bash
docker run --rm -v "$PWD:/src/swarm-core:ro" ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --run "swarm-core-seed --seed=1" swarm-core=/src/swarm-core
```

The microbenchmarks of the per-packet primitives (dev-only):

```bash
cmake -S core -B build/bench -DCMAKE_BUILD_TYPE=Release -DSWARM_CORE_BENCH=ON
cmake --build build/bench --target swarm_core_bench && build/bench/swarm_core_bench
```

A protocol that uses swarm-core runs through its own repository's examples
and through the comparison harness in swarm-routing/ns3-tools.

## Docker and dev containers

`make shell` opens an interactive shell in the `ns3-dev` debug image with
this checkout at `/src`; ns-3 is at `/opt/ns-3` and `ns3-build-contrib
--help` lists the build options. Set `NS3_IMAGE` to use another profile
(`latest-sanitized`, or a pinned `3.48-debug`). The images are documented
in swarm-routing/ns3-containers ("Which image").

There is no `.devcontainer/` yet; a release also publishes this module
prebuilt as the image `ghcr.io/swarm-routing/ns3-swarm-core:<tag>`.

## Lint and format

```bash
# CI uses the org's style file when the repository has none
curl -fsSL -o .clang-format https://raw.githubusercontent.com/swarm-routing/.github/main/.clang-format
git ls-files '*.h' '*.cc' | xargs clang-format-18 --dry-run -Werror   # what `ci / format` runs
git ls-files '*.h' '*.cc' | xargs clang-format-18 -i                  # fix it
rm .clang-format                                                      # it is not committed
make headers          # licence headers and core/'s include rule (fetches the org tools)
make org-sync-check   # the org-managed files match the org templates
ruff check tools && ruff format --check tools   # Python, as python-quality (ruff 0.17.1)
```

`make lint` and `make format` run `clang-format` with whatever version is
first on your `PATH`; use version 18 so the result matches CI. `make check`
runs lint, the ns-3 test, the headers and the org-sync check together.

## CI map

| Workflow | Job (check name) | What it checks | Required |
| --- | --- | --- | --- |
| `ci.yml` | `ci / ns-latest debug`, `ci / ns-latest sanitized` | the module builds with strict warnings and `test.py -s swarm-core` passes in each image; determinism; coverage | yes |
| `ci.yml` | `ci / format` | clang-format-18 on every `.h`/`.cc` | yes |
| `ci.yml` | `ci / headers / licence` | licence headers, `core/` includes nothing outside the standard library | yes |
| `ci.yml` | `cxx-standard` | `core/`'s C++ standard is the image's ns-3 minimum | yes |
| `ci.yml` | `python-quality / python` | ruff and the tests of `tools/` | no |
| `ci.yml` | `codecov`, `bench` | coverage upload; benchmark delta comment | no (report-only) |
| `core.yml` | `core` | `core/` alone: debug, ASan + UBSan, the consumer check | yes |
| `api-surface.yml` | `api-surface` | `docs/api-surface.txt` still matches the headers and TypeIds ([api-policy.md](api-policy.md)) | no |
| `org-sync.yml` | `check / org-sync` | org-managed files match the templates | yes |
| `pr-title.yml` | `title / title` | the PR title is a lowercase Conventional Commit | yes |
| `review-gate.yml` | `review / coderabbit` | CodeRabbit has reviewed the PR | yes |
| `dco.yml`, `scorecard.yml`, `sync-labels.yml` | | sign-off on outside contributions; OpenSSF Scorecard; labels | no |

The required checks are the ones the `main` ruleset lists: `gh api
repos/swarm-routing/ns3-swarm-core/rules/branches/main`. `main` merges
through a merge queue, so a pull request is merged by enabling auto-merge.

## Release

Releases follow the org procedure: [CONTRIBUTING "Cutting a release"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#cutting-a-release).
Here, `release.yml` is dispatched with `dry-run`, then `bump`; merging the
bot's `chore(release): bump version A to B` PR tags the version and
publishes the asset `swarm-core-<ver>.tar.gz` (built by `make dist`; `DIST_CHECK=0`
skips its checks), the image `ns3-swarm-core:<tag>` and a vcpkg port bump.
A dry run also builds the consumers in `.github/downstream.txt` against the
candidate. What counts as a breaking change is in
[docs/api-policy.md](api-policy.md).

## Troubleshooting

- **`make test` is slow on Apple silicon or fails with an exec format
  error.** The `ns3-dev` images are `linux/amd64` only; Docker runs them under
  emulation. `make shell PLATFORM=linux/amd64` selects the platform
  explicitly.
- **A warning fails the ns-3 build.** `--strict-warnings` turns warnings in
  this module's sources into errors (ns-3's own headers are exempt). Fix the
  warning; the `core` job uses the same flags, so `make core-test` usually
  shows it first.
- **`ci / format` fails but `make lint` passed.** Your `clang-format` is not
  version 18, or the org's `.clang-format` was not in the checkout; see
  "Lint and format".
- **`core/` fails to compile with `std::span` or `std::bit_cast` errors.**
  The compiler is older than GCC 13 / Clang 17 or not in C++23 mode.
- **A protocol repository's `swarm-core / pin` check goes red after your
  `core/` change merges.** That is expected: each protocol pins swarm-core and
  needs a pin bump (AGENTS.md "Where to look", "Merge a change to `core/`").
- **The determinism gate fails.** Something reaching the output depends on
  wall-clock time, libc `rand()` or unordered-container order (golden rule 5).
