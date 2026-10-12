# ADR-0007: Deploy native Codex with a Gateway-owned capability

## Status

Accepted. Supersedes the supervised runtime and socket transport of
[ADR-0004](0004-unpublished-supervised-agent-runtime.md). Its no-host-port policy
and [ADR-0005](0005-profile-owned-broker-trust.md) remain in effect.

## Context

agent-workloads GH-34 / ADR-017 replaces the published OpenClaw/Codex supervisor
with a native Codex App Server. Gateway must authenticate to its private
WebSocket listener. The client manages deployment, while Gateway owns device
credentials. Codex must not obtain the capability token that would let it
approve its own actions through its App Server.

## Decision

- Keep the persisted workload ID `openclaw-codex`, container name
  `amnezia-openclaw-codex` and image repository. Display **Codex** in the UI.
  Image labels may name the native workload `codex`; deployment ownership labels
  retain `openclaw-codex`, because client guards use that identity.
- Gateway creates the token in its private state volume and atomically publishes
  only its digest to `/run/amgpt-codex/app-server-token.sha256`. Amnezia does not
  generate, read, persist, hash or pass either value. Remove `AMGPT_CODEX_APP_SOCKET`;
  use the image's default `ws://codex-app-server:4500` endpoint.
- Retain `amnezia-agent-codex-app-socket` at `/run/amgpt-codex` as the shared
  digest volume: Gateway read-write, Codex read-only. Add volume permissions to
  the canonical spec hash, shell rendering and actual mount observation.
  Matching labels cannot hide writable digest-mount drift. Invalid/missing
  observed permissions are an unknown observation, never proof of equivalence.
- Runtime uses alias `codex-app-server`, only its Codex home/workspace private
  volumes, no added capabilities, `/tmp` tmpfs and stop timeout 20 seconds.
  Preserve image user, entrypoint and command. Inherit its exec healthcheck,
  expecting the Node `fetch` readiness probe on `127.0.0.1:4500/readyz`; keep
  client timing overrides 10/5/3/60.
- After Gateway create/recreate is healthy, find the exact runtime name and
  verify both `managed-by=amnezia-agent-app` and `workload=openclaw-codex`.
  Restart that container by ID with a 20-second stop timeout, then require
  Docker `healthy` within 90 health observations. Missing runtime or foreign
  labels are skipped; ambiguous identity, inspect/restart failure, stopped
  runtime, absent healthcheck or timeout prevents apply success. Ordinary
  Gateway start does not restart the runtime.
- Either install order is supported. Runtime-first starts a healthy locked
  listener; Gateway installation publishes the digest and restarts it. Health
  alone does not prove authenticated App Server access or check-in.

## Alternatives considered

- Client-owned token storage and container-command digest: introduces an
  unnecessary persistent client secret and couples both deployment specs.
- Runtime-readable raw token: lets the agent authenticate to its own App Server.
- Socket bridge/root supervisor: superseded by Codex's native authenticated
  WebSocket listener.

## Consequences

Both image generations and the client contract must be deployed together.
Old client/new images lack the alias; new client/old runtime lacks its required
capabilities. `:dev` is mutable and is not immutable rollout evidence.

This release requires explicit removal, fresh volumes and a new device login.
Reconciliation never silently erases Codex or Gateway state. Explicit runtime
uninstall still purges the two obsolete private OpenClaw volumes, including
leftovers without a container. The shared digest volume/network survive while
any container references them, including stopped containers. Legacy Codex-state
migration is not verified.

Restarting an existing runtime interrupts its active sessions. If Gateway
creation succeeds but runtime restart fails, the apply fails with partial
remote state retained. An unchanged Gateway observed later is not evidence
that this restart succeeded; explicitly restart the runtime and inspect both
workloads before testing check-in/WebUI.

The authoritative workload source is
[ADR-017](https://github.com/amnezia-gpt/agent-workloads/blob/main/docs/decisions/ADR-017-codex-only-published-runtime.md)
and its deployment handoff. The inspected digest-owner revision was an
uncommitted workload working-tree change, not a published image.
Validation boundaries and the implementation checklist are in
[the client plan](../sdlc/codex-native-runtime-plan.md).
