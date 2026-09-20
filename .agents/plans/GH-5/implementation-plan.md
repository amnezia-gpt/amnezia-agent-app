# GH-5 implementation plan: apply agent workload deployment plans over SSH

## Outcome and route

Implement a narrow `planned-change` for the two agent workloads only. The
client accepts validated desired state and a reconciliation plan from GH-3 and
GH-4, re-observes immediately before mutation, executes one embedded workload
playbook through the existing `SshSession` command runner, waits for bounded
container health, and re-observes before reporting success.

This issue supplies the core apply API. GH-6 will connect it to lifecycle UI
and persisted server operations. No legacy container is migrated to the new
path.

## Ownership and contracts

- `agent-workloads` release `v0.1.0` owns executable contents and admissible
  runtime inputs. This repository consumes its published Linux/amd64 images.
- `AgentWorkloadDeploymentSpec` owns exact image, environment, labels, port,
  volumes, tmpfs, network, restart policy, health check, and security values.
- `AgentWorkloadReconciliationPlan` owns the permitted action. Embedded scripts
  execute that declaration and enforce only local safety invariants; they do
  not choose product policy.
- `SshSession` owns transport, bounded output collection, uncertain-outcome
  classification, and pre/post mutation observation.

## Stable apply contract

Add typed outcomes:

- `Applied`: a mutating action completed and post-apply observation is `NoOp`;
- `NoOp`: fresh pre-apply observation is already `NoOp`;
- `Failed`: validation, stale plan, known script failure, health timeout, or
  deterministic post-apply drift is proven without ambiguity;
- `Unknown`: a mutating command may have run but its terminal result or the
  required post-apply observation cannot be proven.

`Conflict` and `Unknown` reconciliation actions are hard mutation barriers.
Every invocation re-observes and recalculates the plan before mutation. If the
fresh action differs from the requested action, the operation stops; it never
silently applies a different plan.

The mutation command emits a small terminal status marker. A known failure or
health timeout received over an intact command channel maps to `Failed`.
Transport loss, cancellation, oversized output, or a missing terminal marker
after mutation begins maps to `Unknown`. A later retry must start with a new
observation.

## Ordered implementation

### 1. Strengthen desired-state validation

Anchors:

- `client/core/models/agentWorkloadDeploymentSpec.{h,cpp}`
- `tests/selfhosted/contracts/agent_workload_deployment_spec_test.cpp`

Expose validation for a complete already-constructed specification, not only
factory inputs. It must accept exactly the canonical auth-proxy and OpenClaw
specifications produced by the existing factories and reject mutated workload,
image, platform, runtime, environment, label-driving, health, and security
fields before SSH opens.

Evidence: focused QtTest RED/GREEN rows for canonical and mutated specs.

### 2. Add safe embedded apply playbooks

Anchors:

- `client/core/utils/selfhosted/scriptsRegistry.{h,cpp}`
- `client/server_scripts/serverScripts.qrc`
- `client/server_scripts/amgpt-auth-proxy/`
- `client/server_scripts/openclaw-codex/`
- `tests/selfhosted/contracts/container_registry_contract_test.cpp`

Register one folder for each workload with a contract Dockerfile and
`run_container.sh`. Render only a validated spec. Every scalar is encoded as a
single POSIX shell argument; no raw desired-state value becomes executable
syntax. Rendering must fail on missing assets, unresolved placeholders, or an
unsupported plan.

The playbooks:

- use `set -eu` and bounded health polling;
- create/reuse the declared bridge network and named volumes idempotently;
- pull the exact image for `linux/amd64`, then run with `--pull=never`;
- apply the declared environment, labels, port, mounts, tmpfs, restart,
  capabilities, security options, health check, and stop timeout;
- start only the exact managed target for `Start`;
- verify target ownership immediately before `Start` or `Recreate`;
- force-remove only that verified target during `Recreate` and never remove
  volumes or the shared network;
- print no endpoint values and discard noisy Docker output.

Evidence: resource/manifest checks, shell syntax checks, hostile-value quoting
tests, exact option/label/environment assertions, sibling-name absence, and
deterministic script-result fixtures.

### 3. Orchestrate typed apply through SshSession

Anchors:

- `client/core/models/agentWorkloadApply.{h,cpp}`
- `client/core/utils/selfhosted/sshSession.{h,cpp}`
- `tests/selfhosted/ssh/ssh_session_test.cpp`
- `client/cmake/sources.cmake`

Add `applyAgentWorkloadPlan(credentials, desired, requestedPlan)`. It validates
the spec and plan locally, performs fresh observation, renders the appropriate
embedded script, executes it as one bounded command through the existing
runner, classifies its terminal marker, and performs structured post-apply
observation. The apply output cap is independent from Docker pull volume.
Standard error is discarded so container/runtime diagnostics cannot leak
future sensitive values into logs.

Evidence: focused fake-runner tests for Create, Start, Recreate, NoOp, stale
plan, Conflict/Unknown rejection, preflight failure, quoting, known script
failure, health timeout, cancellation/transport loss, oversized output,
post-observation drift, and post-observation failure.

### 4. Preserve independent lifecycle boundaries

Use tests to prove every generated command names only the selected workload,
its own volumes, and the shared network. Recreate must retain named volumes;
no path removes the sibling, an unmanaged container, unrelated volumes, or the
network. Backend-profile changes render only the auth-proxy playbook.

Removal itself remains a GH-6 lifecycle operation because GH-4 defines no
`Remove` reconciliation action. GH-5 provides the ownership checks and
targeted mutation primitives that GH-6 will reuse.

### 5. Document and expose the remote-smoke boundary

Update the desired-state ADR and validation documentation with apply outcomes,
TOCTOU ownership guards, bounded health behavior, and unknown-outcome rules.
Keep `just smoke agent-workloads-remote` explicit and failing until the GH-1
disposable-host credentials/scenario are supplied; document the exact future
sequence rather than reporting an unexecuted remote success.

## Acceptance evidence

- focused RED/GREEN/REFACTOR evidence for validation, rendering, and SSH apply;
- `just quality check`;
- `just quality test`;
- `just quality client-build`;
- `git diff --check`;
- full diff and security review before squash merge into GH-1.

The local gates do not prove remote Docker execution, public registry
availability, privileged service behavior, signing, packaging, or other
platforms. Disposable Linux/amd64 smoke remains required before GH-1 closes.

## Authoritative references

- <https://docs.docker.com/reference/cli/docker/container/run/>
- <https://docs.docker.com/reference/cli/docker/container/start/>
- <https://docs.docker.com/reference/cli/docker/container/rm/>
- <https://docs.docker.com/reference/cli/docker/image/pull/>
- <https://docs.docker.com/reference/cli/docker/network/create/>
- <https://docs.docker.com/reference/cli/docker/volume/create/>
