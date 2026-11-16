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

<!-- Seeded by org-sync from swarm-routing/.github templates/rcapa/README.md;
the repository's own from here. Replace every TODO with this project's facts
(its AGENTS.md, release.yml and docs), then delete this comment. -->

## The flow for this project

| | |
|---|---|
| Published as | TODO: the release channels this project ships (GitHub release + Zenodo DOI, PyPI, a vcpkg port, images, a site deploy, a dataset release, a plugin version), or "nothing yet: the flow starts at the first release" |
| ID prefix | TODO: `<PREFIX>-NNN` (CONTRIBUTING "RCAPA", the prefix table) |
| Ticket | an issue in swarm-routing/ns3-swarm-core, form *Regression in a release (RCAPA)*, title `[rcapa] <PREFIX>-NNN <short description>`, labels `rcapa` and `bug` |
| Verified by | TODO: the check, gate or reproducer that shows the regression and its fix |
| Corrective action | TODO: the fix and its test, and how a fixed release is cut |
| Preventive action | TODO: the check that would have caught it before the release |
| Closes when | the fix is merged **and** a release of the channel above carries it |

```mermaid
sequenceDiagram
    participant U as Reporter
    participant T as RCAPA ticket
    participant D as This page
    participant M as main
    participant R as Release
    U->>T: regression found against a release
    T->>D: RCAPA document and index row
    T->>M: fix and test, merged
    M->>R: release that carries the fix
    R->>T: tag and DOI recorded, ticket closed
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
