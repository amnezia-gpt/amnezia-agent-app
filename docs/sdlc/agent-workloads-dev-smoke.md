# Disposable-host dev acceptance run

## Prerequisites

The image owns the healthcheck executable (exec-form `CMD`). The client must
not pass `--health-cmd`, which forces `CMD-SHELL` and breaks the shell-free
Auth Proxy image. Client timing overrides and bounded Docker-health polling
remain in place. Health means liveness before login, not `/readyz` readiness.
See [workload contract update](https://github.com/amnezia-gpt/agent-workloads/issues/10).
No Docker API migration, shell installation, or OpenClaw image change is needed.
An existing failed proxy deployment on `v0.2.0` must be reconciled/recreated
through the client with `v0.2.1`; merely restarting it retains the broken probe.
Existing-container observation uses `docker container inspect --format`;
`--type container` is not supported by that container-specific subcommand.
The corrected read-only observation command was exercised against the failed
deployment and returned a valid schema-1 JSON envelope. This does not yet prove
successful replacement or login.
Docker may report a single wildcard port publication as both `0.0.0.0` and
`::` bindings. Reconciliation accepts either address family or their pair
when every binding has the expected host port, container port, and TCP
protocol. Extra/different mappings and unexpected bind addresses remain drift.
Optional shell argument lines must preserve command continuation when empty.
A deterministic test now executes both rendered create scripts with an
intercepted Docker function and verifies that the run argv includes the image,
port, and trailing health options. Syntax-only `sh -n` cannot detect this gap.

- A dedicated disposable Linux x86_64/amd64 VM, reachable from the desktop by
  SSH, with root or non-interactive sudo suitable for Amnezia's installer.
- Outbound DNS/HTTPS access for OS packages, Docker Hub and the selected provider.
- Auth Proxy `v0.2.1` and OpenClaw/Codex `v0.2.0` images. Use the release manifests to check
  the digest pair; do not change the desktop to consume a moving tag.
- A built macOS desktop client and an SSH credential entered through its UI.
- For AMGPT only: deployed dev Auth device authorization, browser verification,
  token/refresh/exchange support and Router compatibility. Check backend issue
  `amnezia-gpt/amgpt-router#496`; an image release is not backend evidence.

Before installing the proxy, select **Agent workload environment** in the app's
**Dev console**: `local` for the local backend exposed through its public tunnel,
or `dev` for the shared development backend (the default). This setting is
independent of **Dev gateway environment**. Production is not selectable.
For `local`, enter the public HTTPS Auth issuer and Router base URL (`/v1`) in
the Dev console and choose **Save local backend**. There are no local endpoint
defaults. Both URLs are validated and saved together under
`Conf/localAgentBackendProfile`; an incomplete/invalid profile blocks install
before SSH. Personal tunnel addresses must never be added to source or tests.
These settings are included in application backups, and resolved addresses are
stored in the remote deployment configuration. Treat exported backups and
deployment diagnostics as private; do not attach them unredacted to issues.
The choice is persisted under `Conf/agentWorkloadEnvironment`. The resolved
Auth/Router pair is saved atomically in each new proxy deployment (`local` or
`development` profile). Inspection, login, lifecycle and repair retain that saved
pair even after the global selection changes. To switch a deployed backend,
explicitly reinstall it using the new selection; changing the setting alone does
not contact the host. Missing/invalid saved profiles fail closed.
If Dev console is hidden, open the About page and click the software version
12 times to enable the existing developer menu.

Keep workload host ports restricted to the test environment; browser device
approval talks to the provider, not directly to a VM callback port. Do not open
all VM ports for this test. No local Docker or privileged VPN service is needed.
Manual disposable-host acceptance has been exercised; see the dated evidence below.

## Local gate and launch

Run `just quality check`, `just quality test`, `just quality client-build` and
`git diff --check` using the configured Qt/Conan environment. Local builds use
at most two compilation jobs. The `local-client` preset produces:

```sh
open deploy/build/presets/local-client/client/AmneziaVPN.app
```

If using the existing non-preset Debug build directory, launch instead:

```sh
open deploy/build/client/AmneziaVPN.app
```

Record the actual build path and worktree revision. An app bundle from a prior
successful build is not evidence for unbuilt source changes.

## Through-client scenario

The QML source flag `nativeLoginEnabled` defaults to `false`: native ChatGPT
sign-in and its status action are hidden. AMGPT remains visible. Native test
steps below require an explicit source-flag change and rebuilt client; native
protocol support is retained but is not part of the current exposed product UI.

1. Add the disposable host in Amnezia and install **OpenClaw + Codex** alone.
   Wait for deployment to finish, then refresh its state. Expect healthy and
   converged. Do not deploy the proxy as an implicit part of this action.
2. Select **Sign in with Amnezia GPT**. Expect an explanation of the missing
   proxy and navigation to its installation page, with no automatic installation.
3. Return to OpenClaw and select **Sign in with ChatGPT**. Expect a clickable
   HTTPS authorization presentation and plain user code. Open the browser,
   close the desktop app after receiving the presentation, and approve there.
4. Reopen the app and workload page. Use **Refresh ChatGPT sign-in status**
   without first starting another login. Expect ready after provider approval.
   Confirm the stale link/code is no longer displayed.
5. Stop OpenClaw through its lifecycle UI, start it again, and refresh native
   status. Expect the completed session to persist.
6. Independently install **Amnezia GPT Proxy**, choosing a free host port.
   Confirm the client applies the development Auth/Router tuple together.
   Expect container liveness before authentication; `/readyz` may still be 503.
7. If the backend prerequisite is available, start **Sign in with Amnezia GPT**,
   close the desktop after receiving the presentation, approve, reopen and
   refresh AMGPT status. Expect ready. Repeat the stop/start persistence check.
   Otherwise record this step as blocked, not passed.
8. Stop the proxy and attempt AMGPT login. Expect install/repair guidance and
   no automatic restart. Native login remains independent of this dependency.
9. Refresh a converged deployment repeatedly. Expect no replacement and an
   unchanged container ID. Apply the same desired state through the controller
   acceptance path when automating this run; expect `NoOp`.
10. On this disposable host only, deliberately stop a managed container and
    use the app to start it. For a drift scenario, change the managed
    container's restart policy, refresh, and explicitly repair. Expect a
    recreate plan, restored desired state and retained named-volume data.
11. Exercise a conflicting name/port using a disposable test container. Expect
    Conflict with no deletion of the foreign container. Remove only that test
    fixture after the check. Record exactly which fixture was used.
12. Remove one workload via the app. Expect the other container and persistent
    volumes to remain. Removal does not revoke the remote provider session or
    wipe credentials; destroy the dedicated VM after testing when appropriate.

## Evidence and completion

Record client revision/worktree, image release/digests, OS/architecture,
scenario step and outcome. Record container IDs only where needed to prove
NoOp or replacement; use redacted screenshots without links/codes/credentials.
Never attach full `docker inspect`, environment, workload homes or raw login
stdout. Never record provider tokens, private device codes or SSH keys.

GH-7 local completion and GH-1 remote acceptance are separate. The dated evidence
below covers only exercised scenarios, not every lifecycle/negative case.
`just smoke agent-workloads-remote`
still fails deliberately: a manual checklist must not turn an unimplemented
automated gate into a successful no-op. Use this runbook for the first manual
run and record evidence before replacing that gate with automation.

## Manual evidence — 2026-09-20

The operator used the macOS Debug client from the GH-7 worktree to install
the two independent workloads on fresh Ubuntu 24.04 amd64 DigitalOcean VMs
(Singapore, 2 vCPU / 4 GiB). Docker installation was performed by the client,
not preinstalled by the test runner. Auth Proxy used v0.2.1 and OpenClaw/Codex
v0.2.0, as pinned by this client revision.

- Local backend through operator-owned public tunnels: installation, AMGPT
  browser approval, ready status and two sequential model replies passed.
- Fresh shared-dev deployment: the client environment was selected before
  installation. Inspection confirmed the dev Auth/Router pair in proxy runtime
  configuration. Both containers were healthy and workloadctl reported ready.
- The dev endpoints were reachable directly from the VM without NetBird on
  that VM: discovery returned HTTP 200 and anonymous Responses returned 401.
- Two sequential prompts in one new session (greeting, then capabilities)
  returned nonempty responses with exit 0, no abort and no fallback. OpenClaw
  reported approximately 23s and 18s for the dev turns with gpt-5.6-luna.
  The previous second-turn HTTP 422 did not reproduce against the updated backend.

This proves the manual install -> AMGPT login -> multi-turn generation path,
not native ChatGPT login, tool execution, restart persistence, every repair/
conflict scenario, automated remote gating or other desktop platforms.
The DO token cannot delete droplets (403); VM destruction remains an explicit
operator action. A stopped VM is not proof that billing has ended.
