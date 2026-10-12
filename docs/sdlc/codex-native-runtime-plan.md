# Native Codex runtime deployment plan (agent-workloads GH-34)

## Scope and contract

Adapt the client to agent-workloads ADR-017. Device Gateway owns the App Server
token and writes only its digest to the existing shared named volume. The
client neither stores nor transfers the token, digest or runtime command.
Keep legacy workload, container and image identities to preserve ownership
guards and saved configurations. No host listener, TLS provisioning or backend
dependency is added to the client.

## Implementation and acceptance

1. Update `agentWorkloadDeploymentSpec` and `agentWorkloadApply`: remove the
   Gateway socket environment entry; add volume `readOnly` to the hash and
   shell rendering; give native Codex its DNS alias, two private Codex volumes,
   read-only digest mount, no added capabilities and image-owned readiness.
   Prove desired-state validation, hash changes and exact Docker arguments.
2. Update `agentWorkloadReconciliation` and the bounded `SshSession` observation:
   inspect mount permissions and detect writable digest drift even with matching
   labels. Test malformed observations and unchanged mounts.
3. Update the Gateway apply playbook: after create/recreate health succeeds,
   restart an existing runtime only if both ownership labels match. Wait for
   its health with a separate bounded runtime budget. Do not restart on start.
   Exercise absent/foreign runtime, both install orders, restart and health
   failures through an executable Docker fake. Never emit success on failure.
4. Replace OpenClaw product text with Codex, retaining serialization identities.
   Update ADRs and smoke commands. Explicit uninstall still removes legacy
   private OpenClaw volumes; apply does not silently destroy old data. This
   release requires fresh volumes and a new login; old-state migration is not
   claimed.
5. Run focused RED/GREEN tests, `just quality check`, `just quality test`,
   `just quality client-build` with two build jobs, and `git diff --check`.
   Review the actual UI metadata and existing QML checks. Local evidence does
   not prove authenticated App Server connectivity, check-in or a WebUI turn.

## Rollout and validation limits

The two image tags and client contract must be deployed together. Old and new
runtime generations are incompatible; container health alone is insufficient.
The inspected workload contract is currently an uncommitted working-tree
change. Publication, merging and remote state changes are outside this task.
Live pair acceptance requires the matching published images, fresh volumes,
`codex_app_auth: published`, an unlocked runtime, login, check-in and a WebUI
session. A failed runtime restart after Gateway creation is a partial apply;
recover by explicitly restarting the runtime and inspecting both workloads.

## Evidence (2026-10-12)

Client working tree: branch `codex/10-runtime-gateway-profile`, based on
`1303b0f7`, with pre-existing profile, enrollment and links work retained.
Workload source: `05a27b9` plus the uncommitted GH-34 digest-owner changes.

- **RED:** focused native-spec tests failed on missing `codex-app-server` and
  the retained Gateway socket env. Executable apply tests failed on absent
  sibling restart and false success after expected restart/health failures.
  Mount-permission drift was incorrectly planned as no-op.
- **GREEN:** deployment-spec suite 72 cases, reconciliation 21, SSH 81;
  all passed against production implementations. Shell tests intercept Docker
  and exercise ownership, create/recreate versus start, restart/health failure,
  timeout, and legacy-volume cleanup without remote mutation.
- **Satisfied:** `just quality check` (5/5 contract suites),
  `just quality test` (11/11 first-party suites, including UI models and QML),
  `just quality client-build` (macOS desktop target), with the configured local
  preset and two build jobs. `git diff --check` and local documentation links
  passed. UI metadata uses Codex; serialization identities remain unchanged.
- **Existing warnings:** Qt macOS deployment-target mismatch and unrelated
  ignored `nodiscard` results. These did not fail the build.
- **Not exercised at the desktop-gate stage:** a live remote image pair, authenticated App Server dial,
  check-in and WebUI session, CLI inference, interactive desktop visual review,
  privileged service and other OSes. No workload publication or backend change
  is proved by the desktop checks. Issues, commits and publishing are outside
  this implementation task.

## Follow-up client-operated remote acceptance (2026-10-12)

The operator installed the published `:dev` pair on a fresh disposable Ubuntu
24.04 x86_64 host through the built Amnezia client, deliberately installing
Codex first. Remote inspection was read-only; no manual container restart or
enrollment repair substituted for client behavior.

- Codex-first: native image user `10100:10100`, correct network alias,
  read-only digest mount, only Codex home/workspace private volumes, no added
  capabilities and no published ports. Docker health and `/readyz` passed;
  the startup log reported a locked listener and unauthenticated WebSocket
  upgrade returned HTTP 401, as expected before Gateway installation.
- Gateway installation: both containers became healthy; Gateway logged
  `codex_app_auth` with outcome `published`. Runtime start time advanced after
  Gateway creation. Its running App Server command used the published digest,
  without reading or returning the raw capability token. The new startup log
  no longer reported a locked listener. Both workload-network aliases matched.
- Device login: management reported `authenticated=true`, `state=ready`;
  ingress recipient status was `enrolled`. Connectivity became `active/ok`
  with repeated successful check-ins and no recorded connectivity errors.
  The current `links-v1` command announced a WebUI link.
- The operator then confirmed that WebUI worked. This is operator-reported
  acceptance; the assistant did not independently capture a WebUI session or
  model response.

This satisfies the runtime-first installation and login/connectivity path on
the tested published pair. Live Gateway-first installation, rotation/reinstall,
uninstall/partial-failure recovery, CLI inference and other platforms remain
outside this acceptance. Host connection details, credentials, device IDs and
complete launch URLs are intentionally excluded from this public evidence.
