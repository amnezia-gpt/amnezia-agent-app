# GH-6 implementation plan: independent agent workload lifecycle UI

## Intended behavior

The existing self-hosted service catalog exposes `amgpt-auth-proxy` and
`openclaw-codex` as separate installable services. Every explicit lifecycle
operation for those two services uses the typed desired-state, observation,
planning, and apply path delivered by GH-3 through GH-5. Legacy Amnezia
containers retain their current behavior.

Users choose only the workload and host TCP port. The client resolves and
persists the complete backend profile for the auth proxy. OpenClaw remains
independent and receives no backend endpoints. Neither workload implicitly
installs, updates, stops, or removes its sibling.

## Current gap

- The catalog, protocol configs, ports, embedded scripts, desired-state model,
  observation, planner, and SSH apply implementation already exist.
- `InstallController` still routes the two workloads through the legacy blind
  Dockerfile/setup sequence and legacy removal.
- `InstallUiController` only exposes start/stop/status for Telegram services.
- Installed agent workloads fall through to the generic protocol page, which
  has no workload status or repair/update actions.
- No client environment resolver currently populates the auth proxy's atomic
  backend profile.

## Delivery route

This is a planned, client-only change. It touches the core self-hosted
controller, the SSH lifecycle boundary, the UI controller, QML navigation, and
deterministic tests. The workload image contract is unchanged.

## Ordered implementation

### 1. Add typed lifecycle results and guarded SSH operations

Discovery anchors:

- `client/core/models/agentWorkloadReconciliation.*`
- `client/core/models/agentWorkloadApply.*`
- `client/core/utils/selfhosted/sshSession.*`

Add a small typed public lifecycle result for inspect/reconcile, stop, and
remove. Stop and remove must first observe the exact canonical target and
verify Amnezia deployment ownership. Missing is an idempotent no-op; an
unmanaged exact-name target is a conflict; transport loss after a possible
mutation is unknown. Removal preserves named volumes, the shared network, the
sibling container, and all unrelated resources.

Evidence: focused SSH tests prove owned/missing/conflict/command-failure paths
and exact target isolation.

### 2. Route agent installs and repairs through typed reconciliation

Discovery anchors:

- `client/core/controllers/selfhosted/installController.*`
- `client/core/repositories/secureAppSettingsRepository.*`
- `client/core/models/protocols/amgptAuthProxyProtocolConfig.*`

Introduce an agent-only controller path that:

1. creates the normal persisted `ContainerConfig` from the selected port;
2. resolves one complete production or development backend profile from the
   existing client environment selection;
3. creates and validates canonical desired state before SSH mutation;
4. observes and plans;
5. applies only `Create`, `Start`, or `Recreate`, while preserving `NoOp`,
   `Conflict`, and `Unknown` as typed outcomes;
6. persists configuration only after an applied or converged result.

The existing legacy setup/update/remove functions remain unchanged for every
other `DockerContainer`.

Evidence: focused controller tests begin RED and cover both independent
workloads, atomic profile selection, no-op, conflict, unknown, update/repair,
stop, remove, and sibling preservation at the command boundary.

### 3. Expose asynchronous lifecycle state to QML

Discovery anchors:

- `client/ui/controllers/selfhosted/installUiController.*`
- `client/ui/qml/Components/SettingsContainersListView.qml`
- `client/ui/utils/pageEnum.h`

Add agent-workload UI operations and signals with bounded enum/status/reason
fields. Operations run through the existing asynchronous controller pattern,
set/clear busy state, and report cancellation, conflict, failed, unhealthy,
and unknown results without exposing raw SSH or Docker output.

Evidence: deterministic controller-facing tests cover result-to-presentation
mapping. QML contains no Docker policy or endpoint selection.

### 4. Add one shared agent workload settings page

Add `PageServiceAgentWorkloadSettings.qml`, register it in `qml.qrc` and
`PageEnum`, and route both workload cards to it. The page provides:

- current lifecycle/health explanation;
- refresh/inspect;
- explicit install repair/update/start action selected from the typed plan;
- stop when the owned workload is running;
- guarded removal using the existing confirmation/navigation conventions;
- the persisted host port and backend profile as read-only deployment
  information, without raw Auth or Router fields.

The page never installs the sibling and does not include device-login actions.

Evidence: resource/navigation contract checks plus the macOS client build.

### 5. Reconcile documentation and run gates

Update ADR-0001 only where lifecycle behavior becomes implemented rather than
planned. Run:

- focused RED/GREEN tests for each behavioral slice;
- `just quality check`;
- `just quality test`;
- `just quality client-build`;
- `git diff --check`.

## Acceptance criteria

- Both services are separately visible, persisted, inspected, reconciled,
  stopped, and removed.
- All mutation decisions for the two workloads come from typed observed state;
  QML contains no shell/Docker policy.
- Auth and Router coordinates are resolved as one profile and are never user
  editable or independently mixed.
- Port or ownership conflicts cause no mutation and remain retryable.
- A failed or uncertain operation is never presented as success.
- One workload operation leaves its sibling, volumes, and shared network
  untouched.
- Existing containers continue through the legacy path with unchanged tests.

## Validation boundary

Local deterministic tests and the macOS client build can prove controller
selection, command construction, parsing, state mapping, QML registration, and
compilation. They do not prove Docker behavior on a real Linux slave. The
disposable Linux/amd64 install/readiness/restart/removal scenario remains the
parent GH-1 integration gate.

## Rollback

Reverting this sub-issue returns agent workload operations to their prior UI
state without changing persisted legacy container formats. Remote workloads
already deployed by the typed path remain ordinary Docker containers with
restart policy and persisted volumes; no automatic rollback mutation occurs.
