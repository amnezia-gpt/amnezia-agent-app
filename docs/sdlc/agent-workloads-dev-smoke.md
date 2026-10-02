# Disposable-host dev acceptance run

## Prerequisites

The image owns the healthcheck executable (exec-form `CMD`). The client must
not pass `--health-cmd`, which forces `CMD-SHELL` and breaks the shell-free
Device Gateway image. Client timing overrides and bounded Docker-health polling
remain in place. Health means liveness before login, not `/readyz` readiness.
See [workload contract update](https://github.com/amnezia-gpt/agent-workloads/issues/10).
No Docker API migration, shell installation, or OpenClaw image change is needed.
Old Auth Proxy deployments are a superseded pre-production contract. The
client does not silently adopt, rename or remove them.
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
- Device Gateway from the temporary compatibility reference
  `docker.io/amneziavpn/agent-workload-amgpt-auth-proxy:dev`, and the
  OpenClaw/Codex `dev` reference configured in the client. The repository name is legacy;
  the deployed workload identity remains Device Gateway.
- A built macOS desktop client and an SSH credential entered through its UI.
- For AMGPT only: deployed dev Auth device authorization, browser verification,
  token/refresh/exchange support and Router compatibility. Check backend issue
  `amnezia-gpt/amgpt-router#496`; an image release is not backend evidence.

Before installing the Device Gateway, select **Agent workload environment** in the app's
**Dev console**: `local` for the local backend exposed through its public tunnel,
or `dev` for the shared development backend (the default). This setting is
independent of **Dev gateway environment**. Production is not selectable.
For `local`, enter the public HTTPS Auth issuer, Router base URL (`/v1`) and
Runtime Gateway base URL (without `/v1`) in the Dev console and choose
**Save local backend**. There are no local endpoint
defaults. All three URLs are validated and saved together under
`Conf/localAgentBackendProfile`; an incomplete/invalid profile blocks install
before SSH. Personal tunnel addresses must never be added to source or tests.
These settings are included in application backups, and resolved addresses are
stored in the remote deployment configuration. Treat exported backups and
deployment diagnostics as private; do not attach them unredacted to issues.
The choice is persisted under `Conf/agentWorkloadEnvironment`. The resolved
Auth/Router/Runtime Gateway profile is saved atomically in each new Gateway
deployment (`local` or
`development` profile). Inspection, login, lifecycle and repair retain that saved
profile even after the global selection changes. To switch a deployed backend,
explicitly reinstall it using the new selection; changing the setting alone does
not contact the host. Missing/invalid saved profiles fail closed.
If Dev console is hidden, open the About page and click the software version
12 times to enable the existing developer menu.

Agent workloads publish no host ports (ADR-0004); browser device approval talks
to the provider, not directly to a VM callback port. Do not open VM ports for
this test. No local Docker or privileged VPN service is needed.
Manual disposable-host acceptance has been exercised; see the dated evidence below.

### Disposable DigitalOcean host contract

Create the disposable host as an Ubuntu 24.04 amd64 DigitalOcean droplet. Its
SSH endpoint is TCP port `48273`; do not expose or use the default port `22`.
Attach the `amnezia-agent-dev-smoke` firewall/tag and restrict the inbound
`48273/tcp` source to the current operator public IPv4 `/32`. When the
operator's public IP changes, update that source before diagnosing SSH as a
host failure.

Ubuntu 24.04 activates OpenSSH through `ssh.socket`. Bootstrap configuration
must therefore set `Port 48273` in `sshd_config` and replace the socket's
listeners with both `0.0.0.0:48273` and `[::]:48273`. Verify the effective
configuration and listeners before opening the client:

```sh
sshd -T | grep -E '^(port|passwordauthentication|permitrootlogin) '
ss -ltn | grep 48273
```

Use a bootstrap SSH key only to provision the disposable host and set the root
password. Do not enter or transmit that key through the client. The client
connection fields for this scenario are the droplet's public IPv4, SSH port
`48273`, username `root`, and the operator-controlled password. Never commit or
record the droplet IP or password in this repository.

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

1. Add the disposable host in Amnezia and install **AMGPT Device Gateway**.
   The wizard offers no port. Confirm the client applies the selected
   Auth/Router/Runtime Gateway tuple together, the container becomes healthy and
   `docker port amnezia-amgpt-device-gateway` prints nothing.
2. On the Gateway page, start **Sign in with Amnezia GPT**. Expect a clickable
   HTTPS authorization presentation and plain user code. Open the browser,
   complete the approval, then refresh status. Expect ready. Close and reopen
   the desktop before refreshing once to prove detached login management.
3. Stop and start the Gateway through its lifecycle UI, then refresh AMGPT
   status. Expect the completed session to persist.
4. Independently install **OpenClaw + Codex**. Wait for deployment to finish
   (the runtime health start period is 60 s), then refresh its state. Expect
   healthy and converged, no published port, and
   `docker exec amnezia-openclaw-codex node /opt/workload/bin/runtimectl.mjs status`
   reporting every service ready. Its page must not show ChatGPT or AMGPT login
   controls.
   After both services are healthy and login is ready, read:

   ```sh
   docker exec amnezia-amgpt-device-gateway workloadctl connectivity status --json
   ```

   Expect `state=active` and `last_outcome=ok`. The gateway requests the
   connectivity scope at login and checks in automatically; the desktop does
   not start or poll check-in. Repeat this proof on both local and shared dev.
   Login `ready` alone is not check-in evidence. If connectivity is in backoff,
   record its bounded outcome and restore the selected Runtime Gateway/Auth
   dependency before claiming success. No broker/WebUI session is required.

5. If the backend prerequisite is available, repeat **Sign in with Amnezia GPT**,
   close the desktop after receiving the presentation, approve, reopen and
   refresh AMGPT status. Expect ready. Repeat the stop/start persistence check.
   Otherwise record this step as blocked, not passed.
6. Stop the Gateway and attempt AMGPT login. Expect a not-ready result and no
   automatic restart.
7. Refresh a converged deployment repeatedly. Expect no replacement and an
   unchanged container ID. Apply the same desired state through the controller
   acceptance path when automating this run; expect `NoOp`.
8. On this disposable host only, deliberately stop a managed container and
    use the app to start it. For a drift scenario, change the managed
    container's restart policy, refresh, and explicitly repair. Expect a
    recreate plan, restored desired state and retained named-volume data.
9. Exercise a conflicting name/port using a disposable test container. Expect
    Conflict with no deletion of the foreign container. Remove only that test
    fixture after the check. Record exactly which fixture was used.
10. Remove one workload via the app. Expect the other container and persistent
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

This evidence predates ADR-0003 and documents the superseded Auth Proxy image
pair; it is historical evidence, not validation of the current Device Gateway
contract. The operator used the macOS Debug client from the GH-7 worktree to install
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
