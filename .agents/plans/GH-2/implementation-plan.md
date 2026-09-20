# GH-2 implementation plan: self-hosted deployment validation gates

## Intended outcome

The repository gains one reproducible validation harness for the existing
Amnezia self-hosted container deployment engine. The harness uses the native
project stack: CMake 3.25 presets, CTest, Qt Test, Qt tooling, and thin `just`
entry points. It establishes characterization coverage before GH-1 changes the
engine, so new agent workloads can reuse behavior already proven for existing
containers.

The production scope is the deployment lifecycle only:

```text
ContainersModel
    -> InstallController
    -> InstallerBase and concrete installers
    -> ContainerUtils / scriptsRegistry / embedded server scripts
    -> SshSession / libssh::Client boundary
```

The plan does not attempt to test the whole Amnezia client. Self-hosted config
import/export, VPN client management, tunnel operation, the privileged local
service, Network Extension, updates, unrelated QML pages, and vendored code are
outside scope.

## Existing surface and constraints

- The selected deployment surface contains 37 first-party C++ files and about
  6,377 lines of C++/headers.
- `client/server_scripts` contains 64 embedded assets (44 shell scripts and 12
  Dockerfiles) across 12 container directories, about 2,327 lines in total.
- `InstallController` is 1,661 lines and constructs concrete `SshSession`
  instances internally.
- `SshSession` owns a concrete `libssh::Client`, so deterministic orchestration
  tests require a narrow injected session/factory seam.
- The root build does not enable CTest and contains no first-party Qt Test
  targets. Tests found under vendored dependencies do not cover this product.
- Existing behavior is the specification unless a test exposes a clear defect.
  Behavioral fixes require separate review and must not be hidden inside test
  enablement.

## Gate contract

Public entry points:

```text
just quality check
just quality test
just quality client-build
just smoke agent-workloads-remote
```

- `quality check` performs repository-owned static and resource consistency
  checks without contacting a remote host.
- `quality test` configures/builds the first-party test targets and runs CTest
  with output on failure and an error when no tests are discovered.
- `quality client-build` builds the existing desktop client target and does not
  claim service, signing, installer, mobile, or remote-host coverage.
- `smoke agent-workloads-remote` is explicit and opt-in. It must never run merely
  because unit tests or a normal pull-request gate run.

Checked-in CMake presets define shared configure/build/test behavior.
`CMakeUserPresets.json` remains ignored and owns developer-specific Qt paths.
`just` is an ergonomic dispatcher; CMake and CTest remain the source of truth.

## Ordered implementation tasks

### 1. Add the native test harness without product behavior changes

Files and anchors:

- `CMakeLists.txt`
- new `CMakePresets.json`
- new `tests/CMakeLists.txt`
- new `Justfile` and `just/quality.just`, `just/smoke.just`
- `.gitignore`

Changes:

- Include CTest at the top level and add `tests/` only under `BUILD_TESTING`.
- Define first-party test targets separately from the application executable;
  do not enable or count vendored dependency tests.
- Add CTest labels `unit`, `contract`, `qml`, `integration`, and `remote`.
- Set bounded timeouts, output-on-failure, and no-tests-as-error behavior.
- Provide a committed preset suitable for CI and an inheritable local preset
  contract without committing a machine-specific Qt installation path.
- Make every public `just` recipe runnable from the repository root and make
  `just --list` sufficient to discover it.

Evidence:

- Configure succeeds with both `BUILD_TESTING=ON` and `OFF`.
- `ctest --show-only` lists only first-party targets.
- The client target remains buildable with testing disabled.

### 2. Characterize container metadata, registry, and embedded assets

Files and anchors:

- `client/core/utils/containerEnum.h`
- `client/core/utils/containers/containerUtils.{h,cpp}`
- `client/core/utils/selfhosted/scriptsRegistry.{h,cpp}`
- `client/server_scripts/serverScripts.qrc`
- `client/server_scripts/**`
- new tests under `tests/selfhosted/contracts/`

Coverage:

- Data-drive every existing `DockerContainer` value through string identity,
  human metadata, service type, default protocol, supported/current-platform
  flags, shareability, install order, and fixed-port behavior.
- Prove each deployable container maps to the intended script directory and
  required protocol script names.
- Prove each registry path exists in `serverScripts.qrc`, each QRC entry exists
  on disk, and deployment assets are not silently omitted from the application.
- Parse every shell asset with the appropriate shell syntax checker and apply
  static checks only to first-party scripts.
- Exercise variable generation and substitution with deterministic fixtures,
  including metacharacters and missing placeholders; tests must not print
  credentials or substituted secrets.
- Keep unsupported `Cloak` and `ShadowSocks`, and the shared Xray/SSXray mapping,
  explicit in the test table instead of silently skipping them.

Evidence:

- Contract tests cover all current enum rows and all 64 embedded assets.
- Adding an enum without registry/QRC classification makes the gate fail.

### 3. Characterize configuration generation in every installer

Files and anchors:

- `client/core/installers/installerBase.{h,cpp}`
- all concrete files under `client/core/installers/`
- `client/core/models/containerConfig.h`
- protocol config types consumed by the installers
- new tests under `tests/selfhosted/installers/`

Coverage:

- Data-drive default port, transport, container identity, generated secrets,
  and container-specific protocol configuration for every installer selected by
  `InstallController::createInstaller`.
- Verify deterministic fields exactly and verify generated secrets by shape and
  invariants rather than fixed values.
- Cover valid boundary values and invalid/missing typed configuration without
  performing SSH, filesystem, or network operations.
- Capture the current update/reinstall comparison behavior for AWG, WireGuard,
  MTProxy, Telemt, and the default branch.

Evidence:

- Every existing installer has at least one happy-path row and applicable
  boundary/error rows.
- The tests link the same production implementation used by the client rather
  than copied source logic or a test-only reimplementation.

### 4. Introduce the smallest SSH seam required for deterministic tests

Files and anchors:

- `client/core/utils/selfhosted/sshSession.{h,cpp}`
- `client/core/utils/selfhosted/sshClient.{h,cpp}`
- `client/core/controllers/selfhosted/installController.{h,cpp}`
- installer constructors that consume `SshSession`
- new fakes under `tests/selfhosted/support/`

Changes:

- Add a narrow session interface or factory covering only the operations used
  by deployment orchestration.
- Preserve the real `SshSession` and libssh implementation as the production
  default; application callers must not need to know about the fake.
- Make ownership and lifetime explicit without changing cancellation behavior.
- Do not introduce a second SSH implementation or change remote commands while
  creating the seam.

Coverage:

- Script line continuation, comment skipping, first-error termination, stdout
  and stderr callbacks, container upload/exec/cleanup sequencing, `sh` versus
  `bash` selection, and error propagation.
- Fake sessions record typed invocations and return scripted results; they must
  not accept arbitrary hidden behavior from individual tests.

Evidence:

- Existing application construction still uses real SSH by default.
- Unit tests execute with network disabled and cannot contact a host.

### 5. Characterize the setup/update lifecycle and representative failure boundaries

Files and anchors:

- `client/core/controllers/selfhosted/installController.{h,cpp}`
- `tests/selfhosted/support/`
- new tests under `tests/selfhosted/install_controller/`

Coverage:

- Prove the fresh-install order: sudo eligibility, package-manager lock,
  Docker availability/install, port check, host preparation, old-container
  cleanup, image build, run, configure, firewall, and startup.
- For representative setup stages, inject an error and prove later remote
  operations do not run and the original bounded `ErrorCode` is returned.
- Cover update versus fresh install, representative setup-stage failures, and
  the existing best-effort cleanup boundaries needed before GH-1 extends the
  setup path. Exhaustive cancellation, removal, status, and rediscovery
  characterization is outside this baseline gate.
- Cover missing/stopped-container daemon responses and unknown remote outcomes
  already represented by current behavior.
- Table-drive container-specific variations instead of cloning test bodies.

Evidence:

- The full happy path and representative early-return boundaries run without
  real SSH.
- Repository mutation is absent on remote failure and present on confirmed
  success according to current behavior.

### 6. Cover the deployment-facing Qt model boundary

Files and anchors:

- `client/ui/models/containersModel.{h,cpp}`
- `client/ui/models/containerProps.h`
- new tests under `tests/selfhosted/ui/`

Coverage:

- Use `QAbstractItemModelTester` plus direct role assertions for container rows,
  installation availability, service/protocol grouping, and model refresh.
- Cover observable model rows, roles, support/install policy, installed state,
  reset notification, and processed selection.
- Isolate settings and files using Qt test mode and temporary directories.
- Test observable model state, not pixel output or unrelated QML navigation.

Evidence:

- Tests do not use fixed sleeps, the real user configuration, SSH, Docker, or
  system browser integration.
- `InstallUiController` remains outside this gate because deterministic tests
  would require new seams across concrete repositories, controllers, and async
  workers unrelated to GH-1 behavior.

### 7. Add CI enforcement and bounded follow-up evidence

Files and anchors:

- new `.github/workflows/quality.yml`
- existing `.github/workflows/deploy.yml`
- new `docs/sdlc/validation.md`

Changes:

- On pull requests, run stable named jobs for static/contracts, Linux unit
  tests, and one desktop client build.
- Keep the existing release/platform matrix distinct from the fast required
  gate; one Linux build must not claim macOS, Windows, mobile, service, signing,
  or packaging coverage.
- Add ASan/UBSan for the first-party unit targets as a non-blocking scheduled
  job until stable enough to promote.
- Document exact claim boundaries and required local prerequisites.
- Configure branch required checks only after the jobs have run successfully
  and their names are stable.

Evidence:

- Local and CI gates call the same checked-in commands.
- CI publishes useful failure output and cannot pass with zero discovered tests.

## Parallel implementation ownership

The first generation wave may run concurrently with strict file ownership:

- **Harness agent:** CMake/CTest presets, `Justfile`, `just/**`, test support
  target structure, and CI skeleton. It does not write component test cases.
- **Registry agent:** `tests/selfhosted/contracts/**` only. It may report needed
  production seams but does not edit shared CMake or production code.
- **Installer agent:** `tests/selfhosted/installers/**` only. It may report
  compile dependencies or defects but does not edit shared CMake or production
  code.
- **Primary agent:** shared integration, production seams, controller/SSH/UI
  tests, conflict resolution, build/test execution, and evidence.

No generated test is accepted merely because it looks plausible. The primary
agent must compile it, run it against production code, inspect failures, and
remove assertions that restate implementation details without protecting an
observable invariant.

## Acceptance criteria

- The four public gate commands are documented and discoverable.
- `quality test` runs only first-party tests and fails when no tests are found.
- Every existing self-hosted deployable container is represented in registry,
  resource, and installer characterization tables.
- Fresh install, update, and representative setup-stage error boundaries are
  deterministic under fake SSH.
- The deployment-facing container model has behavioral Qt tests without real
  user state or fixed sleeps; exhaustive legacy UI-controller lifecycle tests
  are explicitly outside scope.
- No test contacts the network unless invoked through the explicit remote smoke
  command.
- Vendored tests and generated build-tree tests are excluded from coverage
  claims.
- `git diff --check`, the first-party test suite, and the development-platform
  client build are reported separately with exact limitations.

## Rollout and rollback

- Land the work as reviewable commits: harness, registry/assets,
  installers, SSH seam, orchestration, UI boundary, and CI/docs.
- Strict static checks initially apply only to new tests and fork-owned changed
  files. Do not make the inherited upstream tree fail on a legacy warning
  baseline.
- Keep sanitizer and remote smoke jobs advisory until repeat runs establish
  stability.
- The harness can be disabled with `BUILD_TESTING=OFF`; production SSH remains
  the default implementation, so rollback does not require a remote migration.

## Explicit gaps

- A fake-SSH unit suite does not prove libssh interoperability or remote Docker
  behavior.
- One desktop build does not prove other platforms or the privileged service.
- Shell syntax and contract checks do not prove that every Docker image builds
  or that every service becomes ready.
- Full remote coverage of all existing containers requires a disposable host
  matrix and is intentionally separate from the pull-request gate.
