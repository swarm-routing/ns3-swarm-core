# 0001. Record architecture decisions

- **Status:** accepted
- **Date:** 2026-11-09
- **Issue:** —

## Context

Decisions about this repository's structure need to survive the session that
made them, so later contributors (people and agents) can check a change
against the reason, not just the code. AntHocNet records them in
`docs/adr/`, and the org follows the same shape.

## Decision

Structural decisions are recorded as ADRs in `docs/adr/`, from
`0000-template.md`, numbered in order and listed in `docs/adr/README.md`.

## Consequences

A change that contradicts an accepted ADR either follows it or supersedes it
with a new ADR in the same PR.
