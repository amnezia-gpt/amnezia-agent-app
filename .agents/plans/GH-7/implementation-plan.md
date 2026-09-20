# GH-7 implementation plan: device-login presentation

## Outcome

Add explicit `Sign in with ChatGPT` and `Sign in with Amnezia GPT` actions to
the installed `openclaw-codex` workload page. Amnezia starts a bounded,
non-interactive remote login operation, receives one safe browser presentation,
then closes SSH. The workload owns polling, token exchange, token storage,
refresh, and the persistent OpenClaw/Codex session.

## External contract dependency

Implementation depends on `amnezia-gpt/agent-workloads#6`. Before client code
is finalized, that repository must publish the exact versioned contract for:

- the allowlisted native and Amnezia GPT login-start commands;
- the detached-process guarantee: polling survives `docker exec` completion;
- the maximum stdout document size and schema version;
- presentation fields and nullability;
- stable public error codes and exit behavior;
- the separate bounded session-status command and its schema;
- the workload release containing this contract.

The contract is implemented in `agent-workloads` and both immutable `v0.2.0`
tags were verified publicly available on 2026-09-16. The client consumes that
release's schemas; it must not infer command spelling or use a moving image.

Schema v1 uses the released kebab-case error codes, including
`invalid-request`, `unsupported-mode`, `provider-unavailable`,
`malformed-provider-response`, `start-failed`, `start-timeout`,
`background-failed`, `cancelled`, `expired`, `denied` and the schema's explicit
`supervisor-*` codes. Raw provider diagnostics never become UI or log output.
Failure envelopes may have a null mode. Native expiry is nullable.

The workload implementation may use different internal mechanics per mode.
Native login should use the Codex app-server's structured
`account/login/start` result and keep that app-server under workload-owned
supervision. Amnezia GPT login must move the currently caller-driven proxy
polling under workload-owned supervision (or into the proxy itself) and finish
OpenClaw/Codex configuration atomically after authorization.

## Ordered implementation

### 1. Model and parse the public presentation contract

Add a client-owned typed model for login mode, presentation state,
`verification_uri`, optional `verification_uri_complete`, optional user code,
optional expiry, and stable failure classification. Parse exactly one bounded
JSON document. Reject trailing documents, unsupported schemas or modes,
invalid types, a present but non-positive lifetime, control characters,
credentials in URL userinfo, fragments where forbidden, and schemes outside
the documented allowlist. Native Codex presentation is valid without an expiry
because its structured app-server response does not promise one.

The parser returns only typed public fields. Raw stdout, provider bodies,
private device codes, tokens, cookies, and arbitrary errors never reach QML or
logs.

Evidence: RED/GREEN unit tests for both modes, complete and split
presentations, malformed/truncated/oversized output, schema mismatch, URL
validation, expiry, and redaction boundaries.

### 2. Add the narrow SSH command boundary

Add dedicated `SshSession` operations for login start and session-status
refresh. They target only the canonical managed `openclaw-codex` container,
verify ownership through structured observation, render one constant
allowlisted command per typed login mode, cap stdout/stderr, apply a bounded
timeout/cancellation policy, and classify ambiguous transport failure as
unknown/retryable.

No caller supplies shell fragments, container names, executable paths, or
arguments. Receipt of a valid presentation is terminal success for the SSH
operation; the client does not remain attached to workload polling.

Evidence: command-runner tests prove exact command selection, ownership and
health preconditions, output caps, timeout/cancellation, no secret logging,
and immediate completion after presentation.

### 3. Add controller orchestration and dependency checks

Expose typed operations from `InstallController` and asynchronous wrappers in
`InstallUiController`.

- Both login modes require an owned, healthy, converged `openclaw-codex`.
- Native ChatGPT login has no auth-proxy dependency.
- Amnezia GPT login observes `amgpt-auth-proxy` and requires it to be owned,
  running, live through its `/healthz`-based container health check, and
  converged with the client-owned desired state. Authenticated readiness is not
  a precondition: `/readyz` intentionally remains unavailable until login
  succeeds.
- Missing, stopped, non-live, drifted, conflicting, or unknown proxy state is
  presented as an explicit precondition; the client never installs or repairs
  the proxy implicitly.
- The workload does not return a backend profile. Amnezia already owns the
  selected profile and verifies deployment labels/specification. The proxy
  independently fingerprints its resolved Auth/Router coordinates and resets
  incompatible persisted state to `login_required`.
- Session status is refreshed only on an explicit user action.

Evidence: controller tests cover both modes, every dependency state, retryable
failures, authenticated-readiness not being used as a pre-login gate, and the
absence of sibling mutation.

### 4. Extend the shared workload page

On the `openclaw-codex` page, show both sign-in actions only when the workload
is healthy and converged. Present a valid complete verification URL as a safe
clickable action. When only the base verification URI is supplied, show it
together with the user code as plain bounded text. Opening uses Qt's external
URL API only after typed validation; arbitrary rich text is never rendered.

For Amnezia GPT dependency failures, explain the required proxy state and
offer navigation to its existing independent lifecycle page. Do not trigger
installation from the login action. Show pending/authenticated/expired/failed
session state from the separate status command without exposing credentials.

Evidence: focused presentation-state tests, QML resource/navigation checks,
and macOS client build.

### 5. Reconcile architecture documentation and validate

Update `docs/remoteOAuthBrowserRelayPoc.md`: GH-7 uses the detached device-login
presentation contract, not the earlier long-lived loopback callback relay or
stdout scraping design. Preserve the credential-ownership boundary.

Run:

- focused RED/GREEN tests;
- `just quality check`;
- `just quality test`;
- `just quality client-build`;
- `git diff --check`.

The parent GH-1 disposable-host gate remains responsible for proving browser
approval, detached polling, persisted session state, restart survival, and
both released workload images on Linux/amd64.

The detached control contract changes workload process ownership and therefore
ships as a new immutable `v0.2.0` workload release. Existing `v0.1.0` images
remain unchanged. The client updates its desired workload version only after
the new contract and image are published.

## Acceptance boundary

This issue ends at safe authorization presentation and explicit session-status
refresh. It does not add token handling, client-side device polling, an
interactive terminal, Telegram, prompt transport, automatic proxy deployment,
or a generic remote command API.

## Execution evidence (2026-09-16)

- Release dependency satisfied: both Docker Hub `v0.2.0` tags returned HTTP 200
  and expose `linux/amd64` runtime images. No local Docker was used.
- Parser RED: focused test failed on both oversized verification URL fields
  and the schema-valid null-mode failure envelope. GREEN: the same test target
  passes after enforcing the URL limit and handling unattributed failures.
- Controller coverage is test-after for the existing orchestration. A narrow
  configuration-snapshot overload makes the real controller and SSH boundary
  testable without reading personal settings. Twelve cases cover native proxy
  independence, AMGPT dependency states, unhealthy OpenClaw and command failures.
  Tests also assert no uploads or deployment mutations during login.
- QML now exposes both status actions on a reopened healthy workload page,
  routes missing proxy to installation and other dependency failures to its
  lifecycle page, clears non-pending presentation, and renders user code as
  plain text. The page disables actions/navigation while its operation runs.
- `just quality check` passed; `just quality test` passed all 8 first-party
  CTest suites. Both used an ignored local `existing-debug` preset targeting
  the existing Debug directory, with two compilation jobs.
- Qt 6.10.1 `qmlformat` parsed the changed QML successfully without rewriting it.
- `AMNEZIA_CMAKE_CLIENT_PRESET=existing-debug just quality client-build`
  passed with `--parallel 2`, producing
  `deploy/build/client/AmneziaVPN.app` (Debug, arm64 + x86_64).
  Existing Qt deprecation and macOS deployment-target warnings remain; this
  is not a signed installer or a privileged-service validation.
- Final `git diff --check` passed. Review found no critical/important issue in
  the inspected change; visual navigation and real SSH/provider behavior are
  still part of the remote acceptance gap, not inferred from unit tests.
- Initial reconfiguration was blocked by sandbox writes to the Conan cache;
  rerunning the same bounded build with approved cache access succeeded.
- Remote behavior and visual navigation remain unverified until the dedicated
  VM exists. Follow `docs/sdlc/agent-workloads-dev-smoke.md`; the automated
  remote smoke command remains an explicit failing placeholder. AMGPT live
  approval also depends on backend device flow deployment (router GH-496).

### Local/dev environment follow-up

- Added independent persisted `Conf/agentWorkloadEnvironment` selection in the
  existing Dev console/settings layers. New deployments default to `dev`;
  `local` uses the public tunnels to the local backend. No production endpoint
  is assumed or offered, and the upstream Gateway switch is unchanged.
- Existing deployment snapshots are authoritative for observation, login,
  repair and lifecycle. Global environment changes only affect new installs.
- RED: the controller login matrix failed six cases when the global resolver
  was changed after generating the saved config. GREEN: the same matrix passed
  all 12 rows after preserving the saved profile.
- Added test-after coverage using the real settings repository and temporary
  settings storage with encryption disabled: persistence across reconstruction,
  Gateway independence, invalid selection rejection, fail-closed imported
  values, local URL pair and unavailable production profile.
- `AMNEZIA_CMAKE_PRESET=existing-debug just quality test`: all eight suites
  passed. QML parsing and `git diff --check` passed. Builds remain bounded to
  two jobs; no Docker, VM or remote mutation was performed.
- Follow-up contract gate passed all four suites; the macOS desktop build
  passed with two jobs. Existing macOS deployment-target/Qt warnings remain.
  Review found no blocking issue in the environment-selection slice. Visual
  interaction and disposable-host deployment are still unverified.

### Operator-owned local endpoints

- Removed personal tunnel hostnames from implementation and test fixtures.
  Local profile starts empty and is saved as one validated settings map through
  the Dev console. Invalid input preserves the previous pair; missing values
  fail before remote operations. Fixtures use reserved example domains.
- RED confirmed that the previous built-in local profile incorrectly allowed
  installation without operator configuration.
- Corrected settings-test isolation: Qt native organization-based settings on
  macOS did not use the requested temporary INI path. Each run now uses a
  unique synthetic organization and clears its test values after verification.
- Operator endpoint values are intentionally included in settings backups and
  resolved deployments; these artifacts must not be published unredacted.
- Validation: focused local-profile/settings tests passed; `just quality test`
  passed all eight suites, `just quality check` passed four contract suites,
  and `just quality client-build` passed (existing-debug preset, two jobs).
  QML parsing and `git diff --check` passed. The changed/untracked publishable
  files were scanned for known personal domains, user paths and host addresses
  with no matches. Manual UI and remote deployment remain unverified.

### Manual acceptance follow-up — 2026-09-20

- Supersedes the earlier unverified status for AMGPT manual deployment/login:
  fresh disposable Ubuntu amd64 hosts were installed through the macOS client
  against both local-through-tunnel and shared-dev backends.
- Both containers healthy; AMGPT browser approval and workload ready status
  confirmed. Two sequential requests in the same session passed on each backend.
- Dev proxy runtime endpoints were inspected and matched the development pair;
  the VM itself reached dev without requiring the operator's NetBird session.
- See `docs/sdlc/agent-workloads-dev-smoke.md` for scope and residual gaps.
  Native login, restart persistence and full automated remote gating are not
  claimed. DO cleanup requires the operator because the token lacks delete scope.
