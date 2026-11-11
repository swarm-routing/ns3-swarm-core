# Security Policy

`swarm-routing/ns3-swarm-core` is research code for network simulation. This document records its
security posture honestly; it promises nothing the code does not do. The
org-wide policy is [swarm-routing/.github SECURITY.md](https://github.com/swarm-routing/.github/blob/main/SECURITY.md).

## Trust boundary

<!-- Which input is untrusted, the only code that parses it, and what that
code guarantees. A protocol: its wire-format decoder (org invariant 3). A
tool: the parsers of files it reads. If nothing parses untrusted input, say
so in one line. -->
The bounds-checked little-endian readers in `core/` (org invariant 3):
`WireReader` (`core/swarm-wire.h`) and `VisitedPath::read` never read past the
buffer, reject a short or malformed buffer (wrong version byte, bad bool,
oversized count, trailing bytes) and fail closed, with no undefined
behaviour. Roundtrip and seeded fuzz tests in `core/test/test-wire.cc` back
this on every PR, including under ASan and UBSan. Everything above them
(each protocol's header layout and routing decisions) belongs to each
protocol.

## Known, by-design limitations (out of scope)

- No authentication or integrity on anything: a simulation library, not a
  network stack for real radios.
- Releases are 0.x (v0.1.0 on): the API may change in any minor release;
  `kSwarmCoreApiVersion` names the API a build has. Downstream protocols
  build against `main` or pin a release.

<!-- org-sync:begin reporting -->
## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting: the
repository's **Security** tab, then **Report a vulnerability**. If that is
not available, open an issue that only asks for a private contact, and put
no vulnerability details in it. This is a research project maintained on a
best-effort basis: expect an acknowledgement within a few days, not an SLA.
<!-- org-sync:end reporting -->

## Verifying what it ships

Releases from v0.1.0 on (epic #6) are built, signed and published by the
org's release workflow (`swarm-routing/.github` `reusable-release.yml`,
called by `.github/workflows/release.yml`). Each carries
`swarm-core-X.Y.Z.tar.gz` (the module, for ns-3's `contrib/`, with the
dual-licensed `core/`), `SHA256SUMS`, a CycloneDX SBOM
(`ns3-swarm-core-X.Y.Z.cdx.json`) and signed SLSA build provenance for every
asset (`ns3-swarm-core-X.Y.Z.intoto.jsonl`, and the Sigstore bundle
`ns3-swarm-core-X.Y.Z.sigstore.json` for offline checks). The workflow runs
these checks itself before it publishes, and first builds the tarball in a
clean `ns3-dev` tree, runs `test.py -s swarm-core` and builds and tests
`core/` alone, so a release that fails any of them never goes out. With the
[GitHub CLI](https://cli.github.com/):

```bash
sha256sum -c SHA256SUMS --ignore-missing
gh attestation verify swarm-core-X.Y.Z.tar.gz --repo swarm-routing/ns3-swarm-core \
  --signer-workflow swarm-routing/.github/.github/workflows/reusable-release.yml
gh attestation verify swarm-core-X.Y.Z.tar.gz --repo swarm-routing/ns3-swarm-core \
  --predicate-type https://cyclonedx.org/bom
gh attestation trusted-root > trusted_root.jsonl   # once, while online
gh attestation verify swarm-core-X.Y.Z.tar.gz --repo swarm-routing/ns3-swarm-core \
  --bundle ns3-swarm-core-X.Y.Z.sigstore.json --custom-trusted-root trusted_root.jsonl   # offline
```

The signer is the org's reusable workflow; this repository is in the
certificate's workflow-repository extension. Each release also publishes the
image `ghcr.io/swarm-routing/ns3-swarm-core-ns3:vX.Y.Z-optimized` and
`-debug`, the module built on a pinned `ns3-dev` digest, signed with cosign
(keyless): see `swarm-routing/.github` `reusable-ns3-image.yml` for its
verify commands.
