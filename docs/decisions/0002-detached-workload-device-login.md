# ADR-0002: Workload-owned detached device login

## Status

Superseded in part by [ADR-0003](0003-amgpt-device-gateway-migration.md).
The detached, workload-owned login mechanism remains accepted; its owner is now
the Device Gateway rather than OpenClaw.

## Context

An interactive login CLI ties progress to the caller's SSH session and emits
human-oriented output. The desktop needs one safe authorization presentation
and must be able to disconnect before the user approves in a browser.

## Decision

Use the `agent-workloads` schema-v1 `workloadctl login start|status`
contract. A workload-owned supervisor owns the persistent provider process and
polling. Amnezia sends allowlisted non-TTY commands, parses bounded JSON,
presents the verification link and explicitly refreshes status when requested.

The currently exposed client flow supports AMGPT mode only. It executes login
commands inside the owned, converged, live Device Gateway container. OpenClaw
does not expose provider-login controls and is not a prerequisite for creating
or checking a Gateway login. The client owns backend profile selection; it does
not request a backend profile from the login response.

## Alternatives considered

- Loopback callback forwarding requires desktop listeners and tunnel lifetime
  management that the released device flow does not need.
- Parsing terminal output or holding `docker exec` open is not a stable machine
  interface and ties provider polling to the initiating connection.
- Desktop token storage would cross the intended credential ownership boundary.

## Consequences

No local VPN service, listener, or token store is needed. Errors, limits and
nullability must follow the versioned workload schema. URLs/codes remain
transient and cannot be recorded as diagnostic evidence. Native and AMGPT
require separate live-provider validation on a disposable host. Completed
credentials must survive container restart, while pending flows need not survive
replacement. A local build alone cannot prove the remote integration.
