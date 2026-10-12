# Client GH-12 — profile-owned broker trust

## Scope and sequence

Continue `codex/10-runtime-gateway-profile` without discarding its uncommitted
recipient-enrollment follow-up. Finish client #10 (whole local/dev URL profiles
and ready-login enrollment), then deliver #12 trust configuration on that path.
This supersedes #9's delivery work. Do not close issues, commit, push or publish.
Companion repositories are read-only references in this task.

## Contract

See `docs/decisions/0005-profile-owned-broker-trust.md`. Profile v3 includes the
three HTTPS URLs, exact UTF-8 public ingress signing export and SHA-256 pin.
Issuer pin is Auth issuer. Device Gateway receives `AMGPT_INGRESS_TRUST_BUNDLE`
and `AMGPT_INGRESS_TRUST_SHA256`; its existing `AMGPT_AUTH_ISSUER` pins issuer,
and `AMGPT_RUNTIME_GATEWAY_BASE_URL` is the only check-in/session join base.
There is no separate dial coordinate, public listener, device TLS material,
published port, runtime trust discovery or OpenClaw trust environment.

## Ordered vertical increments

1. Retain and validate #10 enrollment behavior in `sshSession.cpp`, controller
   error mapping and authorization polling. Add recovery evidence; do not add a
   second enrollment pipeline or restart login on retry.
2. Extend `AgentBackendProfile`, `AmgptAuthProxyProtocolConfig` serialization
   and complete desired-state validation. Validate exact bundle schema, unique
   public P-256 keys/points, issuer, bounds and pin before SSH. Preserve bytes.
3. Save whole local profiles atomically through the settings repository and
   controller stack. Extend Dev menu and isolate raw clipboard text from the
   QTextDocument preview. The compiled dev catalog needs an owner-approved
   dev export, never a local/test signer fallback.
4. Include trust in the Gateway environment and canonical spec hash. Render
   inserted values exactly once with POSIX quoting through existing SSH apply.
5. Explicit apply resolves the latest selected same-environment profile;
   reject environment/issuer changes. Preserve named state and sibling runtime.
   Save the installation profile only after successful post-apply observation.
6. Observe five allowlisted public values from actual Docker env with bounded
   parsing; labels alone cannot make a mismatched installation converged.
7. Update ADR-0004/0005, profile contract and smoke instructions. Run repository
   gates and actual QML component tests. Review the complete combined diff.

## Test evidence so far

- RED: `selfhosted_agent_workload_deployment_spec_test rejectsProfileWithoutTrust`
  failed on the old implementation because a three-URL/no-trust profile was
  accepted. GREEN: same command passed with complete-profile validation.
- Profile matrix: valid exports, newline pin mismatch, wrong issuer/pin,
  unsupported schemas, duplicates, malformed/oversized/private/symmetric keys,
  bad coordinates and off-curve points; local/dev isolation and persistence.
- Real `/bin/sh` round trip through the base64 playbook transport preserves
  CRLF, UTF-8, trailing whitespace, quotes and placeholder-like strings.
- Reconciliation: actual env drift beats matching labels; signer rotation
  retains state volume; changed issuer/environment is a conflict.
- Real settings/repository with fake SSH: failed apply retains saved profile;
  verified apply saves rotated trust and preserves sibling config; switches
  fail before connecting.
- Enrollment: existing success/negative matrices retained, retry recovers
  after a failed init without starting a new device login.
- UI RED probe: Qt TextArea normalized CRLF. GREEN: real
  PublicTrustBundleInput keeps the raw bundle separately; seven Qt Quick Test
  results pass, including actual paste-button clicks and read-only preview.
- Final `AMNEZIA_CMAKE_PRESET=existing-debug just quality test`: 9/9 suites
  passed after import-version, BOM, numeric-schema and UTF-8 boundary refinements.
- Final `AMNEZIA_CMAKE_PRESET=existing-debug just quality check`: 4/4 contract
  suites passed on the final source.
- Final `AMNEZIA_CMAKE_CLIENT_PRESET=existing-debug just quality client-build`:
  macOS universal x86_64/arm64 desktop target succeeded with two jobs. Existing Qt minimum-deployment
  and legacy OpenSSL/compiler warnings remain unrelated to this change.
- `git diff --check` passed. Changed Dev/workload pages parse with the selected
  Qt qmlformat; the actual trust input passed seven Qt Quick Test results.
- Interactive full-app UI is left to the user by explicit request. No further
  computer-use permission or interactive backend/SSH work is requested.
- Combined implementation reviewed against the backend export and current
  Gateway verifier. No independent client logic blocker remains. At this initial
  validation point, the dev signer artifact and workload #25 were still
  deployment/E2E dependencies; see the subsequent dev artifact update below.
- No commit, push, issue edit/closure or publication performed.

## Explicit dependencies / validation boundaries

- The environment owner's public dev signing export has now been supplied and
  embedded with its computed SHA-256 pin; the missing artifact dependency is
  resolved. Local remains an independent operator-supplied complete profile.
- Paired workload evidence must prove the exact inline env contract, join URL
  validation against the single Runtime Gateway base, session dial-out/bridge
  and loaded-trust diagnostics. Companion source has evolved since the initial
  review; its earlier path-only/absent-Phase-2 state is not a claim about the
  current workload release. Client artifact tests alone do not prove this E2E.
- Backend #578 must coordinate `DEVICES__DEVICE_BASE_URL` with the profile's
  public Runtime Gateway base and run paired admission/join/Codex tests.
- Client tests and Docker env observation do not prove loaded trust or broker
  E2E. No companion source edits, infrastructure mutations or remote installs.


## Legacy Gateway removal regression (2026-10-05)

- Reproduce removal of a saved Gateway profile without bundle/pin before
  changing production code. Keep SSH/Docker at the fake transport boundary.
- Separate removal from deployment-profile validation. Build the removal
  target from the two supported workload enum values; validate canonical
  identity and observed manager/workload labels before mutation.
- Confirm absence after removal, then remove only that saved service entry.
  Missing containers are successful cleanup; foreign ownership, transport
  failures and post-mutation presence preserve the saved entry for recovery.
  At that validation point, named state volumes were preserved. This is
  superseded for explicit uninstall by the subsequent cleanup fix below;
  update/repair still preserves state and the sibling remains untouched.
- RED: `selfhosted_install_controller_test gatewayRemovalDoesNotRequireTrust`
  failed with InternalError for all six removal outcome rows before the fix.
- GREEN: the exact focused command passed all six rows after the fix.
- Security regression rejects unsupported container types, injected names,
  published-port targets and unknown lifecycle actions before SSH.
- `AMNEZIA_CMAKE_PRESET=existing-debug just quality test`: 9/9 suites passed.
- Read-only remote inspection confirmed the remaining Gateway's ownership
  labels; no remote container, image, volume or local settings were deleted.
- `AMNEZIA_CMAKE_PRESET=existing-debug just quality check`: 4/4 contracts passed.
- `AMNEZIA_CMAKE_CLIENT_PRESET=existing-debug just quality client-build`: macOS
  desktop target passed with two jobs; existing Qt deployment-version warnings
  remain. `git diff --check` passed.
- Removal was verified through real controller/repository/SSH production sources
  with fake transport, including failure recovery. Interactive deletion on the
  remote host remains the user's next check; no deletion was performed by CLI.
- Review confirmed strict install/apply validation is retained and removal
  shell input is limited to canonical workload/container identities.


## Workload UI simplification (2026-10-05)

The user superseded the separate profile-apply UI: remove the dedicated button
and signing-key rotation explanation. Retain internal reconciliation for future
integration with Amnezia's backend configuration delivery. Current local testing
uses Dev menu save followed by Gateway removal/reinstallation; dev profile
changes remain compiled into a new client build.

- Removed only the dedicated apply button and its explanation from the workload
  page. Existing state, lifecycle, login and authorization polling controls remain.
- Updated ADR-0005, the OAuth/workload document and the smoke runbook to describe
  the current UI workflow separately from retained core reconciliation behavior.
- No new mirror test for this small reversible UI removal. The selected Qt
  qmlformat parsed the changed page; resource/contract checks passed 4/4 and the
  existing deterministic Qt/QML suite passed 9/9.
- `AMNEZIA_CMAKE_CLIENT_PRESET=existing-debug just quality client-build`
  passed with two jobs, embedding the updated QML. `git diff --check` passed.
  Interactive full-app review remains with the user; no native UI automation,
  remote mutations or publication were performed.


## Embedded public dev signing artifact (2026-10-05)

- Embed the owner-supplied public ES256/P-256 key
  `ingress-1db2aedb3e974f7fbe2d23d006f951ed` for
  `https://agpt-auth-dev.amzsvc.com` in the compiled v3 dev profile.
  The issuer is a plain URL; Markdown link syntax is not part of the JSON value.
- Preserve the supplied public fields and backend export serialization
  (`json.dumps(..., sort_keys=True)` plus final LF): 294 UTF-8 bytes.
  SHA-256: `31a68940a0519b89bf4f50187642d134fac4f406d3b9b10fcd8135004bb8b38e`.
- Add a contract test for the actual QRC resource through the production config
  parser and deployment validator, including exact inline bundle/pin delivery.
  RED: the previous empty trust profile fails with InvalidTrustBundle.
- No backend signer initialization/rotation, remote installation, issue edit,
  commit or publication is part of this artifact update. Live grant/join/Codex
  E2E remains a separate paired runtime check.
- GREEN: the same focused embedded-resource test passes with the supplied key;
  production validation checks curve membership, matching issuer and raw-byte pin.
- `AMNEZIA_CMAKE_PRESET=existing-debug just quality check`: 4/4 contracts passed.
- `AMNEZIA_CMAKE_PRESET=existing-debug just quality test`: 10/10 first-party
  suites passed, including both production QML runtime boundaries.
- `AMNEZIA_CMAKE_CLIENT_PRESET=existing-debug just quality client-build`:
  macOS desktop target passed with two jobs and the updated resource. Existing
  Qt minimum-deployment-version linker warnings remain. Other platforms, service
  packaging and live backend admission/join are not established by this build.
- `git diff --check` passed. No UI layout was changed by this artifact update.


## Explicit client uninstall purges private workload state (2026-10-06)

- User clarified the lifecycle contract: remove through the client means delete
  the workload and all its private persistent state. Earlier state-preserving
  uninstall semantics caused issuer mismatch on local-to-dev reinstallation.
- Trace QML confirmation -> async UI removal -> InstallController -> SshSession
  -> embedded `remove_agent_workload.sh` -> checked cleanup completion.
- Removal uses canonical names only, independent of bundle/profile validity;
  recheck manager/workload labels immediately before deleting an existing
  container. Gateway owns its identity/recipient/login state volume; runtime
  owns its four state/workspace volumes. Never derive deletion names from input
  profile data, arbitrary observed mounts or shell output.
- Purge private volumes even if the container is already absent. Protect any
  in-use volume via non-forced Docker removal. Shared socket/network resources
  remain while any container references them, including stopped consumers.
  Remove the selected image reference only when unused. No global prune.
- Bound the remote playbook to 45 seconds and its public response to 1024 bytes.
  Check its completion marker and post-observe container absence before clearing
  client metadata. Failure or unknown outcome preserves the entry for retry.
- Keep update/repair and same-environment signing-key rotation state-preserving.
  Explicit uninstall/reinstall performs new DCR/login with a new device ID.
- RED: focused SSH lifecycle tests failed for missing cleanup and the early
  NoOp when a container was already absent. Add actual shell execution against
  a fake Docker boundary for both workloads: private-state cleanup, stopped
  sibling protection, retry, ownership race, in-use volumes, failed list and
  false-success deletion. Add controller persistence rows for cleanup failure
  and a missing completion marker. UI confirmation now states data deletion.
- GREEN: focused SSH lifecycle/playbook cases and all eight controller removal
  outcome rows passed. The actual shell test exercises both workload recipes,
  proves sibling private-volume preservation and repeats successful cleanup.
- `AMNEZIA_CMAKE_PRESET=existing-debug just quality check`: 4/4 contracts passed,
  including QRC/disk manifest and shell syntax. `just quality test` with the same
  preset: 10/10 first-party suites passed, including production QML runtime.
- Initial desktop build exposed an incorrect helper name in the async UI removal
  condition; corrected to the existing `isAgentWorkloadContainer` helper.
  Final `AMNEZIA_CMAKE_CLIENT_PRESET=existing-debug just quality client-build`
  passed with two jobs. Existing Qt deployment-version warnings remain.
- `git diff --check` passed. Desktop build proves this macOS target only;
  runtime QML checks cover page loading but not a manual uninstall on the host.
- Live uninstall/reinstall remains a user-operated client test. Do not repair
  the device by executing a separate manual Docker cleanup over SSH.
