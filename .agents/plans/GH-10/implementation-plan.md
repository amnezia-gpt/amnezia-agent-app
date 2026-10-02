# GH-10: Runtime Gateway profiles for local and dev check-in

## Status

Client implementation validated; live acceptance pending. Working on `codex/10-runtime-gateway-profile` from `amnezia`.
Issue: https://github.com/amnezia-gpt/amnezia-agent-app/issues/10

## Outcome

Fresh local and remote dev deployments receive an atomic Auth, Router and
Runtime Gateway profile. Device Gateway owns DCR, login, credentials and
check-in after login and Codex App Server readiness. Keep current dev images,
Sign in and authorization status. Broker/WebUI, token migrations and new
connectivity UI are outside this issue.

## Sources

- agent-workloads `501f598`: deployment profile v2, GH-24 check-in.
- amgpt-router `055b47a2`: dev Helm service base and connectivity v1.
- Client `ae2c5c12`: two containers, socket bridge and detached login schema v1.

## Tasks and evidence

1. RED: profile-v2 JSON round-trip and deployment environment test.
2. Typed profile/config serialization, URL validation, environment and hash.
   Required HTTPS Runtime base has no userinfo, query, fragment or `/v1` suffix.
3. Dev catalog plus atomic local settings/controller/QML input.
4. Production tests for persistence, pre-SSH rejection, hash drift, rendered
   script delivery and OpenClaw isolation.
5. Deployment/smoke documentation; preserve workload-owned credentials.
6. GREEN, `just quality check`, `just quality test`, host client build, QML
   parsing and `git diff --check`; all builds use two jobs.
7. Review final diff and report live-validation gaps.

## Live boundary and observability

Live DCR/login/check-in requires the current gateway image, available backend
and browser approval. Deterministic client tests do not prove live containers.
No new client lifecycle or telemetry: authorization status stays unchanged;
Device Gateway owns connectivity status read during manual acceptance.

## Execution (2026-10-02)

- RED: built the focused deployment-spec target and ran
  `selfhosted_agent_workload_deployment_spec_test runtimeGatewayProfileReachesDeployment`.
  It failed because JSON round-trip lost `runtime_gateway_base_url`.
- GREEN: the same test passed after typed config/profile and environment wiring.
- Added regression proof for local persistence and invalid-save preservation,
  the selected dev coordinate, missing profile rejection before SSH, URL
  normalization and invalid Runtime bases, spec-hash/reconciliation drift,
  generated Docker argv and OpenClaw isolation.
- An intermediate regression build caught an incorrectly extended OpenClaw
  test fixture; corrected it before the final gates. Normalization fixtures
  were extended to the required complete profile.
- Final `AMNEZIA_CMAKE_PRESET=existing-debug just quality test`: 8/8 suites pass.
- Final `AMNEZIA_CMAKE_PRESET=existing-debug just quality check`: 4/4 suites pass.
- `AMNEZIA_CMAKE_CLIENT_PRESET=existing-debug just quality client-build`:
  `AmneziaVPN` desktop target built successfully with two jobs. Existing Qt
  deprecation and macOS deployment-target warnings remain. This does not prove
  the privileged service, signing, installer or other platforms.
- Qt 6.10.1 `qmlformat` parsed the changed Dev menu successfully.
- `git diff --check`, changed-document relative links and publishable diff scan
  passed. No new source/resource registration was needed.
- Review: no Critical/Important finding remains in the inspected client diff.
  Credential ownership, sibling isolation and detached login are unchanged.
- Live evidence is inconclusive: the existing local Device Gateway reports
  login `ready` but connectivity `backoff/token_unavailable`. Both remote dev
  Runtime Gateway health and check-in requests from this workstation return
  nginx HTML 403. No fresh through-client DCR/login/check-in was completed.
- The placeholder remote smoke recipe was not run. Fresh disposable-host
  acceptance on both environments and visual interaction remain pending;
  the smoke document now records automatic check-in proof. No live success is
  inferred from deterministic tests or the existing installation.
