# RCAPA: regressions in a release of swarm-routing/ns3-swarm-core

An **RCAPA** (root cause analysis with corrective and preventive actions) is
the full record of one **regression in a published release** of this
project: something that worked in a released version and broke after a
change. A bug or change that never reached a release needs none (an
ordinary `bug` issue is enough), and an upstream ns-3 bug is an `NS3-NNN` in
swarm-routing/ns3-tools. The org's rule and its decision table are in
[CONTRIBUTING.md, "RCAPA"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#rcapa)
([ADR-0003](https://github.com/swarm-routing/.github/blob/main/docs/adr/0003-rcapa-for-ns3-bugs-and-released-regressions.md)).
This page is the project's flow and its index.

## The flow for this project

A **published release** of swarm-core is a `v*` tag of this repository:
its GitHub release with the `swarm-core-<ver>.tar.gz` asset (`tools/release/dist.sh`),
archived on Zenodo (concept DOI
[10.5281/zenodo.24138628](https://doi.org/10.5281/zenodo.24138628)), the
`swarm-core` vcpkg port in swarm-routing/vcpkg-registry, and the
`ns3-swarm-core` image. Every protocol repository builds on a release through
its lock (`swarm-core.lock`) or pin, so a regression here reaches them all.
A defect introduced after the last tag and fixed before the next one never
reached a release and needs no RCAPA; a defect in a released version needs
one whenever it is found. Whether a commit reached a release is read from
`git tag --contains <commit>`.

| | |
|---|---|
| Published as | `v*` tags: GitHub release (`swarm-core-<ver>.tar.gz`) + Zenodo DOI, the `swarm-core` vcpkg port, the `ns3-swarm-core` image; consumed by AntHocNet, ARA, BeeAdHoc, Termite and HOPNET through their locks and pins |
| ID prefix | `SWC-NNN` |
| Ticket | an issue in swarm-routing/ns3-swarm-core, form *Regression in a release (RCAPA)*, title `[rcapa] SWC-NNN <short description>`, labels `rcapa` and `bug` |
| Verified by | the reproducer against the failing tag and the last good one: the `core` check (`core.yml`: ctest at C++23, ASan + UBSan, `tools/checks/check-core-consumer.sh`), `test.py -s swarm-core` in `ci.yml` (debug, sanitized), the determinism gate (`swarm-core-seed`), `api-surface.yml` for an API change; the consumer that broke (its run linked) |
| Corrective action | the fix PR with a test in `core/test/` or `test/` that fails without it (golden rule 13); a breaking API fix follows `docs/api-policy.md`; then a release bump, the vcpkg port bump and the consumers' lock bumps org-bot opens |
| Preventive action | the primitive test, glue test or consumer check that would have caught it before the tag; a consumer canary (`canary-dependencies`) case if only a consumer showed it |
| Closes when | the fix is merged **and** a `v*` release carries it (tag and version DOI read from Zenodo), and the consumers that broke run green on it |

```mermaid
sequenceDiagram
    participant C as Consumer repository
    participant T as RCAPA ticket
    participant G as core and ci checks
    participant M as main
    participant R as Release, port and locks
    C->>T: a released swarm-core breaks a consumer
    T->>G: reproduce against the failing and last good tag
    T->>M: fix with a primitive or glue test
    M->>R: release bump, vcpkg port and lock bump PRs
    R->>C: consumer CI green on the new release
    C->>T: tag, DOI and consumer runs recorded, ticket closed
```

## Index

| ID | RCAPA | RCAPA ticket | Fixed in |
|----|-------|--------------|----------|

## Adding one: the ticket first

1. Open the RCAPA ticket (the table above). `NNN` is the next free number in
   the index.
2. Copy [`_template.md`](_template.md) to `rcapa-<short-description>.md`
   (lowercase, hyphens: what breaks, not where it was found), and add its row
   to the index in the same pull request.
3. The ticket stays open until the corrective action is merged **and a
   release carries the fix**; record that release in the index.

## What an RCAPA answers

1. **What broke, in which release, and who did it touch?** The symptom, the
   releases that fail and the last that passes, the users, numbers or
   downstream pins it affects.
2. **How do I see it myself?** Exact commands, the release or commit, an
   image by tag and digest when one is involved, seeds, expected against
   actual.
3. **Why did it happen, and why did our checks let it into a release?** Five
   whys, each citing its evidence: a source line at a named commit, a log, a
   run, a comment. An answer without a source is a hypothesis and says so.
4. **What does it look like?** Mermaid diagrams: the failing call chain, the
   types or components involved.
5. **What did we do about it?** Corrective actions (the fix, its PR, the
   release that carries it) and preventive actions (the test or check that
   catches a recurrence). An action not done yet is an open issue, linked.
6. **When?** A dated timeline and every link.

## Writing one

- Every issue and PR reference is fully qualified: `swarm-routing/<repo>#N`,
  also for this repository's own items; an ns-3 item is a link whose text is
  `nsnam/ns-3-dev#N` to its full gitlab.com URL. Never a bare number with a
  hash. In a mermaid block, which cannot hold a hash, name the item in words.
- Mermaid: no HTML entities inside a block; write template arguments as
  words.
- An RCAPA is never deleted. Later findings update its status and timeline.
