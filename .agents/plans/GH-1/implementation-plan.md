# GH-1 implementation plan: agent workload deployment and login presentation

## Goal

Integrate `amgpt-auth-proxy` and `openclaw-codex` into the existing Amnezia
Client self-hosted SSH pipeline. The client deploys and manages the two
workloads independently, then presents browser-mediated device authorization
without owning polling, tokens, or the persistent remote user session.

GH-1 is the integration epic. Work is tracked by native GitHub sub-issues with
their actual repository numbers; this plan does not use local task suffixes.

## Branch model

- Integration branch: `feature/gh-1-agent-workload-integration`, based on
  `amnezia`.
- Create each implementation branch from the integration branch and name it
  from the actual sub-issue number.
- Squash-merge each completed sub-issue into the integration branch.
- Squash-merge GH-1 into `amnezia` only after integrated validation and the
  disposable remote-host scenario pass.

## Responsibility boundary

```text
agent-workloads
    defines the admissible versioned runtime contract

Amnezia Client
    selects stable workload releases
    resolves the atomic backend profile
    declares complete desired state
    observes remote state and calculates a plan
    applies the plan through SSH
    presents safe device-login instructions

embedded SSH/Docker scripts
    execute the typed declaration

remote workloads
    own device polling, tokens, refresh, Codex home, and session state
```

Desired state, observation, planning, apply, and verification are introduced
only for `amgpt-auth-proxy` and `openclaw-codex`. This proof of concept does not
create a generic Ansible-compatible engine and does not migrate legacy Amnezia
containers.

## Locked decisions

- The two workloads are independent from the client's perspective. Install,
  update, repair, or removal of one must not implicitly mutate the other.
- Runtime interconnection is expressed by the workload configuration and
  Docker network.
- Consume stable, published Linux/amd64 releases. Moving `latest` and
  development tags are not valid deployment identities.
- Amnezia Client owns exact desired-state values.
- A client environment resolves to one atomic backend profile containing both
  Auth issuer and Router `/v1` base URL. Users cannot mix these endpoints.
- Only `amgpt-auth-proxy` receives backend-profile values and its runtime
  `backend-profile` label. A profile change must not recreate OpenClaw.
- Runtime deployment labels use `org.amnezia.amgpt.deployment.*`; image-owned
  workload metadata uses `org.amnezia.amgpt.workload.*`.
- Reconciliation occurs only for an explicit user operation. There is no
  background controller.
- Login is post-install. ChatGPT login does not require the proxy; Amnezia GPT
  login requires a healthy proxy but never installs it implicitly.
- The client parses one bounded, versioned login-presentation JSON result and
  may close SSH immediately. The workload continues device polling.
- No generic terminal, PTY, client-side token polling, token storage, or
  automatic browser launch is introduced.

## Delivery graph

### #2 — Establish first-party validation gates for agent-driven development

Status: implemented on the integration branch; retained as the foundation for
the behavior changes below.

Evidence gate:

- `just quality check`
- `just quality test`
- focused macOS client build when production C++ or shared build wiring changes

### #3 — Model versioned desired state for agent workloads

Depends on the published workload deployment contract and stable releases from
[`agent-workloads#7`](https://github.com/amnezia-gpt/agent-workloads/issues/7).

Deliver:

- typed desired specifications for both workloads;
- stable image and workload-version identity;
- atomic client-environment-to-backend-profile resolution;
- network, ports, volumes, restart policy, health check, platform, and security
  configuration;
- deterministic normalized `spec-hash`;
- runtime labels `managed-by`, `workload`, `schema-version`,
  `workload-version`, and `spec-hash`, plus `backend-profile` on auth proxy
  only;
- safe rendering into the existing embedded script registry.

The service/protocol identities and initial persisted port-config foundation
already present on the integration branch are incorporated into this issue's
final desired-state model.

### #4 — Observe agent workloads and calculate reconciliation plans

Depends on #3.

Deliver:

- one bounded, structured remote Docker observation;
- typed `ObservedDeploymentState` parsed from untrusted JSON;
- pure deterministic planning with `Create`, `NoOp`, `Start`, `Recreate`,
  `Conflict`, and `Unknown` actions;
- independent planning for auth proxy and OpenClaw;
- safe, typed explanations for conflicts, drift, and unknown state.

Observation includes identity, image, lifecycle/running state, health, mounts,
network, published ports, restart policy, and relevant deployment labels.

### #5 — Apply agent workload deployment plans over SSH

Depends on #3 and #4.

Deliver:

- embedded Dockerfiles and run/apply scripts using stable images;
- validated rendering of desired state into shell and Docker arguments;
- idempotent network and volume handling;
- targeted create/start/recreate behavior;
- bounded readiness/health verification;
- typed `Applied`, `NoOp`, `Failed`, and `Unknown` results;
- re-observation before retry after cancellation or uncertain transport loss;
- preservation of the independently installed sibling and all unmanaged
  resources.

### #6 — Expose independent agent workload lifecycle in the client UI

Depends on #3, #4, and #5.

Deliver:

- separate service cards and persisted configurations;
- independent install, observe, repair/update, supported start/stop, and remove
  operations;
- user-selected ports using existing Amnezia port-conflict conventions;
- client-owned backend selection without raw endpoint fields in the UI;
- accurate progress and mapping of cancelled, failed, conflict, unhealthy, and
  unknown outcomes;
- no Docker policy embedded in QML.

### #7 — Present ChatGPT and Amnezia GPT device login in the client

Depends on #6 and the published detached login contract from
[`agent-workloads#6`](https://github.com/amnezia-gpt/agent-workloads/issues/6).

Deliver:

- explicit `Sign in with ChatGPT` and `Sign in with Amnezia GPT` actions;
- proxy observation and install/repair guidance for Amnezia GPT login;
- allowlisted non-interactive remote commands;
- bounded, versioned JSON parsing and safe HTTPS URL validation;
- clickable verification URL and optional user-code presentation;
- separate bounded session-status refresh;
- no token, device-code, cookie, callback, or Codex-home ownership in the
  client.

## Execution order

```text
#2 foundation
      |
     #3 desired state
      |
     #4 observe + plan
      |
     #5 apply + verify
      |
     #6 lifecycle UI
      |
     #7 login presentation
      |
 GH-1 remote integration proof
```

Later work may be prepared early, but implementation must not duplicate or
guess an unstable dependency contract.

## Parent acceptance evidence

Before GH-1 is merged:

1. Every linked sub-issue is complete and independently reviewed.
2. `just quality check`, `just quality test`, `just quality client-build`, and
   `git diff --check` pass on the integrated branch.
3. A disposable Linux/amd64 slave proves:
   - independent installation of both workloads;
   - idempotent re-apply;
   - explicit handling of drift, conflicts, and unknown outcomes;
   - both login presentations;
   - browser approval;
   - a persistent usable Codex/OpenClaw session after client disconnect.
4. Durable documentation matches the final deployment, label, observation,
   login, and security contracts.
5. Logs and persisted client configuration contain no authorization code,
   device code, access token, refresh token, cookie, private key, complete
   sensitive callback URL, or remote Codex home.

## Out of scope

- Telegram and other OpenClaw surfaces.
- Sending prompts to the remote agent runtime.
- A generic configuration-management framework for all Amnezia services.
- Background reconciliation.
- Apple Developer signing and the privileged local VPN service.
