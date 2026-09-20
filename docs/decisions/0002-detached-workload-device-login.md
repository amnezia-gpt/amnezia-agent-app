# ADR-0002: Workload-owned detached device login

## Status

Accepted. Replaces the [historical loopback relay proposal](../archive/remoteOAuthBrowserRelayProposal.md).

## Context

An interactive login CLI ties progress to the caller's SSH session and emits
human-oriented output. The desktop needs one safe authorization presentation
and must be able to disconnect before the user approves in a browser.

## Decision

Use the `agent-workloads` v0.2.0 schema-v1 `workloadctl login start|status`
contract. A workload-owned supervisor owns the persistent provider process and
polling. Amnezia sends allowlisted non-TTY commands, parses bounded JSON,
presents the verification link and explicitly refreshes status when requested.

Native login requires only OpenClaw. AMGPT login additionally requires the
independent proxy's owned, converged, live deployment. Authenticated readiness
cannot be a prerequisite to login. The client owns backend profile selection;
it does not request a backend profile from the login response.

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
replacement. A local build alone cannot close the parent integration issue.
