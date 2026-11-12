# CLAUDE.md

**Project specifics live in [`AGENTS.md`](AGENTS.md)** (golden rules,
build/verify, where-to-look), imported below so it survives context
compaction. Org-wide conventions (invariants, issue and label taxonomy) live in
[`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md). This file mirrors
[AntHocNet's CLAUDE.md](https://github.com/swarm-routing/AntHocNet/blob/main/CLAUDE.md), the source repository the
org grew out of; the guidelines further down are general behavioural defaults
layered on top.

@AGENTS.md

<!-- org-sync:begin org -->
**Handoffs are always a ticket.** When a session ends with work in flight, or a
handoff is asked for, open a GitHub issue from the *Handoff* template (label
`handoff`). Never write a handoff file in the repo. Start a new session from
the newest open `handoff` issue, and close it when the work it hands off is
done ([AntHocNet ADR-0013](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md) item 5).

**Every change is a PR to `main`, squash-merged.** Open the PR as soon as the
branch is pushed, so all work is tracked; the squash title is the commit on
`main`.

## Working defaults

Condensed from [andrej-karpathy-skills](https://github.com/forrestchang/andrej-karpathy-skills).

- **Assumptions:** state the ones that matter. Ask only when the choice
  belongs to the maintainer: a default, a published number, a cell, the scope.
  Otherwise pick the sensible option, say which, and keep going. Long
  unattended runs, like campaigns, depend on that.
- **Simplicity:** the least code that solves the task. No speculative
  features, configurability or abstractions for one use.
- **Surgical diffs:** every changed line traces to the request. Match the
  surrounding style. Mention unrelated dead code rather than deleting it, and
  remove only the orphans your own change created.
- **Verifiable goals:** a bug gets a reproducing test first; a refactor passes
  before and after. Finish when the check passes, not when the code looks right.

## Response style — caveman (token economy)

Answer in tight, low-filler prose: drop hedging, pleasantries, and preamble; keep
the substance. Fewer output tokens on every reply, same information.

**Carve-out (the record stays full).** Keep byte-for-byte exact — code,
commands, file paths, error text, benchmark numbers. And keep the *durable
record* detailed, never terse: commit messages, PR bodies, ADRs, `docs/`, and
issue / finding threads. Cross-session traceability depends on those being
complete — AntHocNet's AC_BE_NQOS / A2 investigation was only recoverable
because the issue threads were thorough. Caveman the chat, not the record.

## Agent pitfalls

- `gh api … -F body=@file` reads the file; `-f body=@file` posts the literal
  text `@file`.
- When the permission system refuses a command (a merge, say), stop and report
  it. Never retry it another way, and never ask another agent to run it.
- A PR body's `Closes #N` closes the issue on merge; write `Part of #N` while
  acceptance criteria remain open.
- Before saying a tool or check is missing, look at `main` (`git fetch`,
  `origin/main`), not a stale local checkout.
- Change text inside `org-sync:begin/end` markers only through the template
  in `swarm-routing/.github` `templates/`: one PR there, then org-bot's
  `bot/org-sync` refresh PR in each repository. A hand edit per repository
  stays red on `check / org-sync` (swarm-routing/.github#236).
- In a repository with a merge queue, merge by enabling auto-merge (squash);
  a direct merge (`PUT pulls/<n>/merge`) is refused with HTTP 405. Only the
  required checks (`gh api 'repos/{owner}/{repo}/rules/branches/main'`, which
  `gh` fills from the checkout) gate a merge: a red or pending report-only
  check (`codecov/patch`, Sonar) is triaged, not waited on. A red `dco` on an
  outside contributor's commit still holds the PR until that author signs
  off; on the maintainer's or an agent's commits it is expected
  (swarm-routing/.github#212). `dco` is not a required check and runs on
  `pull_request` only, so enable auto-merge on an outside contributor's PR
  only after `dco` passed on its current head, and disable it before pushing
  to that PR.
- Enable auto-merge only after CodeRabbit has reviewed and its threads are
  resolved: it is not a required check, so auto-merge does not wait for it.
  `python3 -I ../.github/tools/pr-status/pr_status.py wait --review REPO#N`
  (a `swarm-routing/.github` checkout next to the repository) waits for the
  review and goes on with a warning 30 minutes after the PR opened
  (swarm-routing/.github#238).
- Never add a `Signed-off-by` trailer, for the maintainer or anyone else: a
  DCO sign-off is a person's own certification (CONTRIBUTING "Outside
  contributions").
- Releases are cut on request only: dispatch the release `bump` when the
  maintainer asks, once the version's `release:vX.Y.Z` issues are done and
  its exit criteria in the ladder (`docs/roadmap.md`, or the org ROADMAP's
  release train until it lands) are met; never approve a gated publish. A
  failed publish is recovered with "Re-run all jobs", never "Re-run failed
  jobs" (CONTRIBUTING "Cutting a release"). Never type a release's DOI: read
  it from Zenodo.
- Cite a paper in a public issue, PR or doc by its `https://doi.org/<DOI>`
  link, read from its record (`lit-search` confirms it), or, for a work with
  no DOI, by its publisher's or institution's page; never point into or name
  the private paper library there.
<!-- org-sync:end org -->
