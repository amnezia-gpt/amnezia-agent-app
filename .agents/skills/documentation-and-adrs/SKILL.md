---
name: documentation-and-adrs
description: Maintains durable documentation for the Amnezia fork. Use when changing architecture, OAuth/browser relay behavior, self-hosted workload contracts, IPC, security boundaries, branch policy, build procedures, or other decisions future maintainers must understand.
---

# Documentation and ADRs

## What to document

- Keep feature and protocol documents under `docs/` in English unless the
  surrounding document is intentionally Russian.
- Explain ownership, trust boundaries, sequence, failure behavior, sensitive
  data, compatibility, and validation—not merely class and function names.
- Keep commands executable and repository-relative; never publish personal
  absolute paths, credentials, tokens, private hostnames, or local account data.
- Update documentation in the same change that changes its contract.

## ADRs

Create `docs/decisions/NNNN-<slug>.md` for a decision that is expensive to
reverse or spans repositories, such as authorization ownership, relay protocol,
workload discovery, secret storage, or deployment transport.

Use this structure:

```markdown
# ADR-NNNN: Decision title

## Status
Proposed | Accepted | Superseded by ADR-NNNN | Deprecated

## Context
Forces, constraints, and the decision that must be made.

## Decision
The chosen behavior and ownership boundary.

## Alternatives considered
Options and concrete rejection reasons.

## Consequences
Benefits, costs, risks, compatibility, migration, and validation impact.
```

Do not delete superseded ADRs; link the replacing decision in both directions.

## Verification

- Check every relative link and referenced repository path.
- Reconcile terminology with code enums, script names, and protocol fields.
- Run `git diff --check` and scan the complete new text for publication-sensitive data.
- Distinguish planned behavior from implemented and verified behavior.
