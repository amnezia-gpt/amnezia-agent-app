# ADR-0003: Migrate the AMGPT login boundary to Device Gateway

## Status

Accepted

## Context

The published workload contract replaced `amgpt-auth-proxy` with
`amgpt-device-gateway`. Device login management moved with it. The client is an
SSH-driven Docker deployer and already has independent workload lifecycle,
observation and reconciliation paths; changing networking or adding another
client-side protocol layer would expand scope without serving the new contract.

## Decision

Keep the existing two-workload deployment shape and shared Docker network.
Replace the Auth Proxy's external identity, embedded script directory,
container, alias, volume, environment and healthcheck executable with the Device
Gateway contract. While registry pull access for the renamed image repository
is unavailable, pull the Device Gateway payload from the legacy
`agent-workload-amgpt-auth-proxy:dev` repository reference. This is only an
image-distribution compatibility name; every runtime identity remains Device
Gateway. OpenClaw/Codex remains pinned by digest.

The Device Gateway page owns the only exposed authorization UI. The client
issues only `workloadctl login start amgpt --json` and `workloadctl login status
amgpt --json`, through fixed commands in `amnezia-amgpt-device-gateway`.
OpenClaw remains independently deployable and has no provider-login controls.

Existing internal C++ enum and configuration type names containing
`AmgptAuthProxy` remain temporarily to limit an otherwise mechanical migration.
They are implementation details: new serialized protocol keys, container names,
labels, resources and UI strings use Device Gateway identity. Existing old-name
deployments are not silently adopted, removed or migrated.

The current host-port publication and reconciliation mechanics are unchanged.

## Consequences

The client can deploy Gateway plus OpenClaw and complete one AMGPT device login
without requiring OpenClaw to be present. Old pre-production configurations are
not wire-compatible with the new external identity and must be installed as the
new workload. Native ChatGPT login is not exposed. Standalone Codex workload
support remains separate future work.
