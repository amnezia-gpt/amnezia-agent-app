# GH-4 Implementation Plan: Agent workload observation and reconciliation

## Intended behavior

Amnezia Client observes either supported agent workload with one read-only,
allowlisted SSH command, parses one bounded versioned JSON document as untrusted
input, and calculates a deterministic plan without changing the remote host.
The auth proxy and OpenClaw remain independent reconciliation targets.

Issue: `amnezia-gpt/amnezia-agent-app#4`

Baseline: `feature/gh-1-agent-workload-integration` at `5f303694`, including the
typed desired state delivered by #3.

Delivery branch: `feature/gh-4-agent-workload-reconciliation`.

## Stable contracts

### Observation envelope

Version 1 contains only reconciliation-relevant data:

- schema version and target workload identity;
- absent or one exact-name container observation;
- container ID and name, configured image reference and runtime image ID;
- lifecycle status, running flag, and health status;
- restart policy;
- named-volume type/name/destination tuples;
- attached Docker network names;
- published host/container port and protocol tuples;
- the fixed `org.amnezia.amgpt.deployment.*` label allowlist;
- exact-name and desired-port conflict observations.

The command must not return container environment variables, complete Docker
configuration, token state, logs, or arbitrary labels. Output is capped in the
client before parsing. A transport failure, oversized output, invalid JSON,
unknown schema version, duplicate/contradictory identity, or missing
safety-critical field produces `Unknown` and blocks later mutation.

### Plan

The pure planner returns one action and one typed, user-presentable reason:

- absent and conflict-free -> `Create`;
- equivalent managed running and healthy -> `NoOp`;
- equivalent managed stopped -> `Start`;
- managed declaration or observed runtime drift -> `Recreate`;
- an unmanaged exact-name container or desired-port owner -> `Conflict`;
- failed, incomplete, contradictory, unsupported, or transiently
  indeterminate observation -> `Unknown`.

An equivalent running container reporting `unhealthy` is runtime drift and
plans `Recreate`. A `starting` or unavailable required health state plans
`Unknown` so a later apply layer cannot destroy a potentially converging
container. Stable image comparison uses the exact versioned image reference
from #3; a moving tag is never substituted.

## Ordered implementation

### 1. Add parser and planner RED tests

Add a focused QtTest target under `tests/selfhosted/contracts/` linked to the
production reconciliation model. Cover at minimum:

- absent, equivalent running, and equivalent stopped;
- spec/image/runtime drift;
- wrong owner and occupied host port;
- malformed, truncated, oversized, unknown-version, and contradictory JSON;
- SSH command failure represented as unknown observation;
- unhealthy and starting health states;
- backend-profile drift affecting only the auth proxy;
- planning one workload without consulting or changing its sibling.

Run the focused target before production implementation and record the intended
compile/test failure as RED.

### 2. Implement typed observation and pure reconciliation

Add `client/core/models/agentWorkloadReconciliation.h/.cpp` containing:

- typed lifecycle, health, parse-error, action, and reason enums;
- `ObservedDeploymentState` and bounded parsing entry point;
- `AgentWorkloadReconciliationPlan` and a side-effect-free planner accepting
  one desired specification and one observation result.

Parsing must use strict JSON types, reject unsupported or incomplete input, and
retain only the label namespace and resource attributes needed for comparison.
No parser or planner path may log the source document.

### 3. Add the bounded read-only SSH observation

Extend the narrow `SshSession` surface with an agent-workload observation call
that accepts a validated desired specification rather than arbitrary command
fragments. Construct one fixed command from allowlisted container identities
and a validated numeric port. Use Docker formatting to emit selected fields,
plus the minimum read-only Docker/socket-owner queries needed for conflicts.

Accumulate stdout only up to the documented byte limit, keep stderr out of the
JSON payload, propagate connection/command/callback failure, and never print
the captured document. Test command construction and failure/size behavior
through the existing `ISshCommandRunner` fake; no real SSH connection is used.

### 4. Document semantics and run gates

Extend ADR-0001 or add a following ADR section covering observed state, trust
boundaries, action/reason semantics, and why environment/token material is
excluded. Reconcile terminology with #3 and #5.

Required local evidence:

- focused parser/planner QtTest;
- focused SSH observation tests;
- `just quality check`;
- `just quality test`;
- `just quality client-build` because production C++ wiring changes;
- `git diff --check`.

The disposable-host smoke is deferred to #5/parent integration because #4 is
read-only and the user explicitly does not require live dev infrastructure
validation at this stage.

## Rollout and rollback

This issue has no remote side effects and is not wired to continuous or UI
execution. Rolling it back removes only typed observation/planning code and its
tests. #5 must treat every `Conflict` and `Unknown` plan as a hard mutation
barrier and must re-observe after any uncertain transport outcome.

## External ownership and open inputs

`agent-workloads` continues to own image/runtime contracts. The client owns the
inspection schema and reconciliation policy. No additional workload change is
required for #4 because the required runtime identity is already exposed by
the fixed deployment labels from #3.
