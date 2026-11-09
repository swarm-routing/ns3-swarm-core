# Security Policy

`swarm-routing/ns3-swarm-core` is research code for network simulation. This document records its
security posture honestly; it promises nothing the code does not do. The
org-wide policy is [swarm-routing/.github SECURITY.md](https://github.com/swarm-routing/.github/blob/main/SECURITY.md).

## Trust boundary

<!-- Which input is untrusted, the only code that parses it, and what that
code guarantees. A protocol: its wire-format decoder (org invariant 3). A
tool: the parsers of files it reads. If nothing parses untrusted input, say
so in one line. -->
The bounds-checked little-endian readers in `core/` (org invariant 3): they
never read past the buffer, they reject a short or malformed buffer, and they
have no undefined behaviour, backed by roundtrip and fuzz tests in `test/`.
Everything above them (routing decisions) belongs to each protocol. The
readers arrive with #8; until then nothing here parses untrusted input.

## Known, by-design limitations (out of scope)

- No authentication or integrity on anything: a simulation library, not a
  network stack for real radios.
- Downstream protocols build against `main`; there is no release to pin yet.

<!-- org-sync:begin reporting -->
## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting: the
repository's **Security** tab, then **Report a vulnerability**. If that is
not available, open an issue that only asks for a private contact, and put
no vulnerability details in it. This is a research project maintained on a
best-effort basis: expect an acknowledgement within a few days, not an SLA.
<!-- org-sync:end reporting -->

## Verifying what it ships

Nothing is released yet, so there is nothing to verify. Downstream
repositories build against `main`.
