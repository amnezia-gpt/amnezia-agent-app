# ADR-0004: Deploy agent workloads without host ports and adopt the supervised runtime

## Status

Accepted

## Context

`agent-workloads` GH-20 (its ADR-011, ADR-012 and ADR-013) changed the runtime
contract that the client deploys:

- The Device Gateway always runs in managed-device mode. Its private `/v1`
  provider endpoint ignores caller authorization and injects the device's
  Router credential. Network reachability of `/v1` is the authorization
  boundary.
- The `openclaw-codex` image is a root supervisor that starts OpenClaw, a Codex
  App Server and a socket bridge under separate non-root identities. It needs
  exactly `SETUID`, `SETGID` and `KILL`, and its image user must not be
  overridden.
- The Codex App Server listens only on a Unix socket. The bridge exposes it to
  the Device Gateway through one shared named volume. The runtime publishes no
  port.
- Volumes from earlier releases are not migrated.

The client previously published the Gateway `/v1` listener and the OpenClaw
listener on every host interface. With managed-device mode, a published `/v1`
lets anyone who reaches the host spend the owner's compute.

## Decision

- Neither agent workload publishes a host port. The deployment specification
  keeps empty `hostPort` and `containerPort`. The apply playbooks omit
  `--publish`, the observation command skips host-port probing, and
  reconciliation treats any observed published port as runtime drift. A legacy
  container that still publishes a port is therefore recreated.
- Persisted `port` values from earlier configurations are ignored. The setup
  wizard offers no port for agent workloads.
- The runtime specification uses only image-owned environment, drops all
  capabilities, adds `SETUID`, `SETGID` and `KILL`, keeps
  `no-new-privileges`, and mounts fresh volumes for OpenClaw state, OpenClaw
  workspace, Codex home and Codex workspace.
- Both specifications mount `amnezia-agent-codex-app-socket` at
  `/run/amgpt-codex`. It is the only resource they share besides the workload
  network. Both images initialize that directory with the same owner and mode,
  so the install order does not matter.
- The Device Gateway receives `AMGPT_CODEX_APP_SOCKET` and
  `AMGPT_INGRESS_STATE_DIR` as in the workload's base Compose contract.
- Capabilities added to a container are part of the canonical specification
  and its hash, so a changed capability set is declaration drift.
- The port-publication and host-port conflict machinery is kept for a
  specification that publishes a port. The optional WebUI TLS ingress listener
  (trust bundle, certificate, key and one published port) is separate future
  work.

## Consequences

- An installation of this release creates new runtime volumes and requires a
  new device login and DCR registration.
- Image references must move to one GH-20 publication record at the same time
  as this specification. Deploying this specification with pre-GH-20 images is
  unsupported.
- Local agent access to the Gateway or OpenClaw is available only from the
  workload network or through `docker exec` over SSH.
