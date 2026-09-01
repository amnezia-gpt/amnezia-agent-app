---
name: self-hosted-workload-integration
description: Adds or changes a remotely deployed Docker workload in the Amnezia self-hosted SSH pipeline. Use when introducing a DockerContainer, embedded server scripts, workload configuration, lifecycle commands, diagnostics, or a client-to-agent-workloads contract.
---

# Self-Hosted Workload Integration

## Existing deployment path

The client embeds files from `client/server_scripts/serverScripts.qrc`.
`scriptsRegistry` maps a `DockerContainer` to its directory and script names.
`InstallController` then uploads the Dockerfile and executes the existing
build -> run -> configure -> startup sequence through `SshSession`.

## Workflow

1. Define the workload contract before client code:
   - immutable workload/image identity and supported host architecture;
   - required inputs and generated outputs;
   - filesystem, port, volume, privilege, and network ownership;
   - health/readiness and diagnostic contract;
   - install, update, restart, removal, and partial-failure behavior;
   - secret identities and redaction rules.
2. Keep the runnable workload and its container-level tests in
   `agent-workloads`. Do not duplicate its application source in this repo.
3. Trace and update every applicable client registration point:
   - container enum/serialization and `ContainerUtils` metadata;
   - `scriptsRegistry` mapping;
   - `client/server_scripts/<workload>/` assets;
   - `serverScripts.qrc` resource entries;
   - installer/configurator/controller factory paths;
   - UI model, selection, configuration, localization, and persisted config;
   - removal, update, diagnostics, and default-container behavior.
4. Use the existing SSH abstraction. Do not introduce a second deployment
   transport inside a workload feature.
5. Validate substituted variables before shell use. Quote shell values at the
   owning boundary and never place secrets in command arguments or logs.
6. Model cancellation and unknown remote outcomes explicitly. A lost SSH
   connection after `docker run` is not proof that the container did not start.
7. Keep OAuth/browser relay transport separate from token ownership. Never log
   authorization codes, PKCE verifiers, tokens, cookies, or full callback URLs.

## Required evidence

- Resource and registry consistency.
- Client target compiles on the development platform.
- Workload image builds and passes its own checks in `agent-workloads`.
- A disposable Linux host exercises install, readiness, restart/update, and
  removal, including at least one failure path.
- Cross-repository revisions and any unavailable platform checks are reported.
