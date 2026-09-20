# Self-hosted deployment validation

The first-party test gate deliberately covers the existing self-hosted Docker
deployment path rather than the whole Amnezia application. Its current suites
exercise container/asset contracts, installer configuration, SSH command
execution, installation orchestration, and the deployment-facing container
model.

## Prerequisites

- the repository submodules are initialized;
- CMake 3.25 or newer and Conan 2.28 are on `PATH`;
- Qt 6.10 with `qtremoteobjects`, `qt5compat`, and `qtshadertools` is available
  through `CMAKE_PREFIX_PATH` or `CMakeUserPresets.json`;
- `just` is installed for the short commands below.

Machine-specific paths belong in the environment or in the ignored
`CMakeUserPresets.json`; they must not be committed.

Local build entrypoints are deliberately limited to two parallel compiler
jobs. Large Qt/C++ translation units can otherwise exhaust memory even when a
machine exposes many CPU cores. The `local` and `local-client` build presets
enforce this limit, and the `just quality` recipes pass it explicitly. Do not
use bare `cmake --build ... --parallel` locally.

## Local gates

Run the complete self-hosted test gate:

```sh
just quality test
```

This performs `git diff --check`, creates a fresh test configuration, builds
the `amnezia-tests` aggregate target, and runs CTest with output on failure.
The tests use fake SSH execution and do not contact a remote host.

The scheduled Linux sanitizer gate uses a separate `ci-sanitizers` preset so
AddressSanitizer and UndefinedBehaviorSanitizer flags cannot leak into the
ordinary CI or local-client configuration:

```sh
just quality sanitizer-test
```

Validate the CMake graph and run the fast repository, resource, shell, and
self-hosted registry contract suite:

```sh
just quality check
```

Build the existing desktop application with first-party tests disabled:

```sh
just quality client-build
```

These commands default to the `local` and `local-client` presets. The
sanitizer command is intentionally fixed to its Linux CI preset. Override
the local presets with `AMNEZIA_CMAKE_PRESET` and
`AMNEZIA_CMAKE_CLIENT_PRESET` when needed. Only after deliberately assessing
available memory, override the job limit with `AMNEZIA_BUILD_JOBS=<count>`.

## Remote smoke boundary

Remote deployment is intentionally not part of pull-request unit tests. The
placeholder command below is explicit and opt-in:

```sh
just smoke agent-workloads-remote
```

This entrypoint intentionally exits with an error until GH-1 adds the bounded
disposable-host scenario. It cannot delegate to an arbitrary command or report
a successful no-op. The future implementation must require explicit
credentials, avoid logging secrets, enforce a timeout, clean up its containers,
and report an unknown outcome separately when transport is interrupted. Its
minimum scenario is: provision a disposable Linux/amd64 Docker host; apply each
workload independently; apply each a second time and require `NoOp`; stop and
recover one workload through `Start`; change one declaration and verify a
targeted `Recreate`; prove the sibling and named volumes survive; then remove
only test-owned resources and destroy the host.

## Deliberate UI boundary

The current gate characterizes the deployment-facing `ContainersModel` and the
core install setup/update path. It does not unit-test `InstallUiController` or
attempt exhaustive remove, rediscovery, and cancellation characterization.
Those legacy flows depend on concrete repositories, controllers, and async
workers; adding seams solely for this baseline gate would widen production
scope without supporting GH-1. New agent-workload behavior must still add
focused tests at the narrowest existing controller or core boundary it changes.

## Evidence boundaries

A green local test gate proves only the deterministic first-party suites on the
current host. A desktop client build additionally proves that the production
target still compiles for that host. Neither result proves privileged-service,
VPN tunnel, remote Docker, packaging, signing, mobile, Network Extension, or
other desktop-platform behavior. The checked-in quality workflow currently
executes on Linux only; its results do not constitute macOS or Windows
evidence. macOS and Windows remain unverified until their matching CI jobs or
local builds run. Mobile and Network Extension configurations intentionally do
not add these desktop-only tests.
