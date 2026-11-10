# CLAUDE.md

**Project specifics live in [`AGENTS.md`](AGENTS.md)** (golden rules,
build/verify, where-to-look), imported below so it survives context
compaction. Org-wide conventions (invariants, issue and label taxonomy) live in
[`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md). This file mirrors
[AntHocNet's CLAUDE.md](https://github.com/danieljoppi/AntHocNet/blob/main/CLAUDE.md), the source repository the
org grew out of; the guidelines further down are general behavioural defaults
layered on top.

@AGENTS.md

<!-- org-sync:begin org -->
**Handoffs are always a ticket.** When a session ends with work in flight, or a
handoff is asked for, open a GitHub issue from the *Handoff* template (label
`handoff`). Never write a handoff file in the repo. Start a new session from
the newest open `handoff` issue, and close it when the work it hands off is
done ([AntHocNet ADR-0013](https://github.com/danieljoppi/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md) item 5).

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
<!-- org-sync:end org -->
