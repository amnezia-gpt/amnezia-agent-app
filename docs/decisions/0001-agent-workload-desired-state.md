# ADR-0001: Client-owned desired state for agent workloads

## Status

Accepted

## Context

Amnezia Client already deploys self-hosted Docker containers through embedded
scripts executed over SSH. That pipeline behaves as a small configuration
management system, but its legacy container definitions do not expose a typed,
versioned desired state that can later be compared with remote state.

The agent proof of concept adds two independently managed workloads. The
original pair was `amgpt-auth-proxy` and `openclaw-codex`; the current external
identity is `amgpt-device-gateway` and `openclaw-codex`. The workload repository defines which
runtime inputs and Docker properties a released image accepts. The client must
choose exact values, persist the user's deployment selection, and eventually
reconcile the declaration over SSH. Backend Auth and Router coordinates form
one compatibility boundary and must never be selected independently.

This decision applies only to these two workloads. It does not change the
deployment model of existing Amnezia VPN containers.

## Decision

Amnezia Client owns a typed `AgentWorkloadDeploymentSpec` for each agent
workload. A specification includes:

- deployment schema version and stable workload release;
- exact image reference and target platform;
- container name, Docker network and alias;
- host and container ports;
- environment, named volumes and tmpfs mounts;
- restart policy, health check and stop grace period;
- dropped capabilities and security options;
- an optional resolved backend profile.

`amgpt-device-gateway` receives one resolved `AgentBackendProfile` containing its
profile identifier, Auth issuer, Router `/v1` base URL and Runtime Gateway
service base URL. These values are persisted together in
`AmgptAuthProxyProtocolConfig`. Profile resolution
validates and normalizes the entire tuple before producing a desired state.
Missing values, unsupported environments, invalid identifiers, non-HTTPS
coordinates, user information, query strings, fragments, and an invalid Router
path or a Runtime Gateway base ending in `/v1` are rejected.

`openclaw-codex` has no backend profile, no Auth, Router or Runtime Gateway
environment values,
and no backend-profile deployment label. A profile change therefore cannot
change its desired-state hash or require its recreation. Any runtime
connection between workloads is runtime configuration on their shared Docker
network, not an installation dependency owned by Amnezia Client.

Runtime identity uses labels under `org.amnezia.amgpt.deployment.*`:

- `managed-by`;
- `workload`;
- `schema-version`;
- `workload-version`;
- `spec-hash`;
- `backend-profile` on `amgpt-device-gateway` only.

The label namespace is based on the stable Amnezia organization domain rather
than a deployment-stage hostname or Docker Hub namespace. Labels owned by the
workload image remain under `org.amnezia.amgpt.workload.*`.

The specification hash is lowercase hexadecimal SHA-256 over a canonical JSON
representation of normalized desired state. Object keys and semantically
unordered lists are sorted. Credentials and other secrets must never become
part of the desired state or hash. Backend coordinates currently participate
in the Device Gateway hash because they are non-secret runtime configuration, but
configuration objects containing them must not be logged as a whole.

The existing `scriptsRegistry` is the rendering boundary. It converts a valid
typed specification into named placeholders for embedded deployment scripts.
It does not define policy or become a generic Ansible-compatible engine.

The complete flow is deliberately split across the GH-1 sub-issues:

```text
typed desired state (#3)
        |
structured remote observation + pure plan (#4)
        |
bounded SSH apply + verification (#5)
        |
independent lifecycle UI (#6)
```

Invalid desired state must stop this flow before the apply layer performs an
SSH mutation. Issue #3 provides the typed validation result; issue #5 must make
successful validation a precondition of every agent-workload apply operation.

Issue #4 implements the observation and planning boundary. `SshSession` runs
one allowlisted, read-only command for one supported workload and receives one
versioned JSON envelope. The command selects only container identity, lifecycle,
health, restart policy, volumes, tmpfs mounts, networks, published ports, and
the six deployment labels above. It also reports exact Docker owners of the
requested published port and whether another host process is listening on it; ADR-0004 removes
publication from the current workloads, so their observation skips this probe. Environment variables,
arbitrary image labels, logs, and application session files are intentionally
excluded.

The client treats the complete remote document as untrusted and caps it at 256
KiB. Invalid, incomplete, contradictory, oversized, target-mismatched, or
unsupported observations produce `Unknown`. The pure reconciliation planner
then maps a valid observation as follows:

- absent and conflict-free: `Create`;
- equivalent, running, and healthy: `NoOp`;
- equivalent but stopped: `Start`;
- managed image, declaration, runtime, or unhealthy-state drift: `Recreate`;
- an unmanaged exact-name container or another desired-port owner: `Conflict`;
- transient lifecycle/health state: `Unknown`.

`Conflict` and `Unknown` are hard mutation barriers for issue #5. Observation
itself never changes Docker state and is not scheduled in the background.

Issue #5 applies only a caller-supplied `Create`, `Start`, or `Recreate` plan.
Immediately before mutation, the client repeats structured observation and
recalculates the plan. An already-converged target returns `NoOp`; a different
fresh action is treated as a stale request, and `Conflict` or `Unknown` stops
without mutation. The complete desired state is also reconstructed and
validated against the canonical workload declaration before SSH is opened.

Each workload has an embedded, narrowly scoped playbook. It creates or reuses
the shared bridge network and workload-owned named volumes, pulls the exact
`linux/amd64` image, and starts it with `--pull=never`. `Start` and `Recreate`
repeat an exact-name ownership check immediately before their mutation.
`Recreate` removes only that verified container and never removes its volumes,
the shared network, the sibling workload, or unrelated resources.

Health polling is bounded by the declared start period, interval and retry
budget. The remote command emits only a small terminal status marker; Docker
output and standard error are not returned to the application. A proven script
failure or health timeout is `Failed`. Once the mutation command may have
started, transport loss, cancellation, excessive output, a missing marker, or
an unavailable post-apply observation is `Unknown`. `Applied` is reported only
after a final structured observation produces `NoOp`.

Issue #6 exposes this reconciliation path as an explicit lifecycle in the
client. Both workloads use the same settings surface but remain independently
selectable and independently mutable. Refresh is read-only. Install, start,
repair, and update execute the action selected by the typed planner. Stop and
remove repeat structured observation, require the exact canonical target to
carry the expected Amnezia ownership and workload labels, and affect only that
container. A missing target is an idempotent no-op; an unmanaged exact-name
target is a conflict. Removal preserves named volumes, the shared network, the
sibling workload, and unrelated Docker resources.

The client resolves the Device Gateway's local or shared development backend as one
atomic profile from its existing environment selection. GH-10 extends that
profile to include the independent Runtime Gateway coordinate for both local
and dev. Device Gateway owns automatic check-in after device login and Codex
readiness; the client owns only validated deployment input. The resolved
profile is persisted with the container configuration and shown read-only.
Global environment changes affect new installations; existing installations
retain their saved profile. Switching an installed backend requires an explicit
new installation, not a background host mutation.

Immutable digest-qualified image coordinates are the deployment identity agreed
with `agent-workloads`. Moving `latest` or development tags are not acceptable
fallbacks for a release. Current pre-release local/dev work uses both `dev`
channels by explicit owner direction; GH-10 preserves that development policy.
See ADR-0003 for the Device Gateway contract migration.

## Alternatives considered

### Put production defaults in workload images

Rejected because images would become environment-aware and a container could
silently use a backend different from the client declaration.

### Store Auth and Router endpoints as independent user settings

Rejected because mixed development and production coordinates form an invalid
deployment and create an unnecessary user-facing configuration surface.

### Make OpenClaw installation depend on Device Gateway installation

Rejected because the two containers must remain independently installable and
manageable. AMGPT login is owned by the Device Gateway itself and is not an
OpenClaw deployment dependency.

### Introduce a generic configuration-management engine

Rejected for this proof of concept. Typed configuration, embedded scripts and
the existing registry already provide the required narrow playbook boundary.
A generic migration of legacy containers would greatly expand risk and scope.

### Use moving image tags

Rejected because observation and reconciliation require a stable deployment
identity and reproducible desired-state hash.

## Consequences

The client gains a deterministic contract that can be observed and reconciled
without changing legacy services. Each agent workload can be planned and
applied independently. Backend selection is atomic and persisted so an explicit
reinstall or update can reproduce the same declaration.

The client now carries workload release metadata and must update it deliberately
when accepting a new contract version. A configuration change does not trigger
background reconciliation; it is applied only by an explicit user operation.

The current implementation proves typed construction, serialization, complete
desired-state validation, rendering, label selection, hash stability, bounded
observation parsing, reconciliation planning, apply classification,
pre/post-apply orchestration, guarded stop/removal, controller routing, and UI
model registration with deterministic Qt tests. The SSH commands are tested
through the command-runner boundary, but have not yet been executed against a
disposable remote Docker host. The local tests do not mutate a remote host or
prove that referenced images are publicly pullable. Those remain acceptance
gates for the parent integration issue.

## Implementation references

- [Docker inspect CLI reference](https://docs.docker.com/reference/cli/docker/inspect/)
- [Docker command formatting](https://docs.docker.com/engine/cli/formatting/)
- [Docker container listing and publish filters](https://docs.docker.com/reference/cli/docker/container/ls/)
- [Docker container run](https://docs.docker.com/reference/cli/docker/container/run/)
- [Docker container start](https://docs.docker.com/reference/cli/docker/container/start/)
- [Docker container remove](https://docs.docker.com/reference/cli/docker/container/rm/)
- [Docker image pull](https://docs.docker.com/reference/cli/docker/image/pull/)
- [Docker network create](https://docs.docker.com/reference/cli/docker/network/create/)
- [Docker volume create](https://docs.docker.com/reference/cli/docker/volume/create/)
