# RCAPA: regressions in a release of swarm-routing/ns3-swarm-core

An **RCAPA** (root cause analysis with corrective and preventive actions) is
the full record of one **regression in a published library**: a version of
it that another project consumes (a release pinned by a lock, PyPI, a vcpkg
port) worked, and a later one broke. A bug or change that never reached a
published version needs none (an ordinary `bug` issue is enough), a Docker
image needs one only when its composition is the cause, and an upstream
ns-3 bug is an `NS3-NNN` in swarm-routing/ns3-tools. The org's rule and its decision table are in
[CONTRIBUTING.md, "RCAPA"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#rcapa)
([ADR-0003](https://github.com/swarm-routing/.github/blob/main/docs/adr/0003-rcapa-for-ns3-bugs-and-released-regressions.md)).
This page is the project's flow and its index.

## The flow for this project

swarm-core is the library every protocol builds on, so a regression here
reaches all of them. **Published** versions are its `v*` releases as
consumers pin them: `swarm-core.lock` in AntHocNet, ARA, BeeAdHoc, Termite
and HOPNET (the release tarball `swarm-core-<ver>.tar.gz` by URL and
SHA256), and the `swarm-core` vcpkg port at a registry baseline.

**Worked example.** AntHocNet bumps its `swarm-core.lock` to a new release
and a check that passed on the old one fails. The suspect is a bug in the
released swarm-core: open an `SWC-NNN` ticket here, from AntHocNet's finding
(its issue or PR linked), and verify the regression with AntHocNet's failing
check on the new release against the old one. If the cause turns out to be
AntHocNet's own use of the library, the ticket says so and closes, and the
fix is an ordinary bug in AntHocNet.

| | |
|---|---|
| Published as | `v*` releases pinned by the protocols' `swarm-core.lock`, and the `swarm-core` vcpkg port |
| ID prefix | `SWC-NNN` |
| Ticket | an issue in swarm-routing/ns3-swarm-core, form *Regression in a release (RCAPA)*, title `[rcapa] SWC-NNN <short description>`, labels `rcapa` and `bug`, opened from the consumer's finding |
| Verified by | the consumer's failing check on the new release against the old one; then this repository's gates reproducing it: `core.yml` (ctest at C++23, ASan + UBSan, `tools/checks/check-core-consumer.sh`), `test.py -s swarm-core` in `ci.yml`, the determinism gate (`swarm-core-seed`), `api-surface.yml` for an API change |
| Corrective action | the fix PR with a test in `core/test/` or `test/` that fails without it (golden rule 13); a breaking API fix follows `docs/api-policy.md`; then a release, the vcpkg port bump and the consumers' lock bumps org-bot opens |
| Preventive action | the primitive test, glue test or consumer check (`check-core-consumer.sh`, a consumer's canary leg) that would have caught it before the release |
| Closes when | a swarm-core release carries the fix **and** the consumer that found it runs green on its lock bump to that release |

```mermaid
sequenceDiagram
    participant C as AntHocNet, after a lock bump
    participant T as RCAPA ticket
    participant G as core and ci checks
    participant M as main
    participant R as swarm-core release
    C->>T: a check that passed on the old version fails on the new one
    T->>G: reproduce with the consumer's check, new version against old
    T->>M: fix with a test that fails without it
    M->>R: a release carries the fix
    R->>C: the consumer moves to it and runs green
    C->>T: release and green run recorded, ticket closed
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
3. The ticket stays open until the corrective action is merged, a release
   carries the fix, and the consumer that found it runs green on that
   release; record the release in the index.

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
