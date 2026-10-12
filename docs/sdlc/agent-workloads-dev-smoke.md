# Disposable-host dev acceptance run

## Prerequisites

The image owns the healthcheck executable (exec-form `CMD`). The client must
not pass `--health-cmd`, which forces `CMD-SHELL` and breaks the shell-free
Device Gateway image. Client timing overrides and bounded Docker-health polling
remain in place. Health means liveness before login, not `/readyz` readiness.
See [workload contract update](https://github.com/amnezia-gpt/agent-workloads/issues/10).
The native runtime deployment changes both images and client together; see
[ADR-0007](../decisions/0007-gateway-owned-codex-capability.md). This release
requires removing the old pair through the client, fresh volumes and new login.
The older healthcheck fix above alone does not make the new runtime compatible.
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
  native Codex `dev` image in the existing runtime repository. The repository
  and runtime deployment ID `openclaw-codex` are legacy names; both must be
  built from the matching GH-34 / ADR-017 publication.
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

Agent workloads publish no host ports (ADR-0007); browser device approval talks
to the provider, not directly to a VM callback port. Do not open VM ports for
this test. No local Docker or privileged VPN service is needed.
Historical disposable-host evidence below predates ADR-017 and does not prove
the native runtime or Gateway-owned digest handoff.

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
   complete the approval, then click **Refresh Amnezia GPT sign-in status**.
   Expect **Signed in and ready** after recipient enrollment. The desktop must
   not schedule status requests while waiting for browser approval. During a
   manual status request, the browser action and back navigation remain usable;
   additional SSH actions cannot overlap. A failed status query preserves the
   pending link and code for retry.
   Verify `docker exec amnezia-amgpt-device-gateway amgpt-device-gateway manage ingress-status`
   reports `enrolled`. Close and reopen the desktop before refreshing once to
   prove detached login management and idempotent recipient enrollment.
3. Stop and start the Gateway through its lifecycle UI, then refresh AMGPT
   status. Expect the completed session to persist.
4. Independently install **Codex**. Wait for deployment to finish
   (the runtime health start period is 60 s), then refresh its state. Expect
   healthy and converged, no published port, and
   `docker inspect --format '{{.State.Health.Status}}' amnezia-openclaw-codex`
   reporting `healthy`. Its page must not show ChatGPT or AMGPT login
   controls.
   Also test the reverse install order: Codex first is healthy but locked;
   creating Gateway must restart it. Gateway logs must include
   `"msg":"codex_app_auth","outcome":"published"`; current runtime startup
   must not report `listener locked until restart`. Older retained log lines
   from its first locked start are not proof that the restarted listener is locked.
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
10. Remove one workload via the app. Expect its container, private state volumes
    and unused image reference to disappear. The other workload's container and
    private volumes remain; shared socket/network resources remain while in use.
    Repeat after a partial removal to confirm orphaned state is also cleaned.
    Reinstallation requires fresh sign-in and a new device identity. Local
    credential deletion does not revoke an already issued backend token.

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


## Broker trust delivery (client #12)

Use the whole selected v3 profile: three HTTPS coordinates, the backend owner's
public `ingress-authority export-public` JSON and SHA-256 over the **exact export
bytes**. Local and dev must have independent matching exports. Never install a
fixture, recipient private key, private signer or another environment's bundle.
The compiled dev catalog is
`client/server_scripts/amgpt-device-gateway/backend-profile-dev.json`; its empty
trust fields require an owner-approved export before dev deployment. Do not
substitute the local signer to make dev appear ready.

For local, choose **local** in Dev menu, fill the three URLs, paste the public
bundle using **Paste public signing bundle**, enter its pin, then save. The
read-only preview can normalize CRLF visually; the raw pasted input remains
unchanged. Invalid saves must preserve the previous settings. Hash a controlled
public export with `shasum -a 256 public-ingress-bundle.json`; do not hash a JSON
reformatted by another tool. A pin is not recalculated silently on invalid input.

Device Gateway alone receives `AMGPT_INGRESS_TRUST_BUNDLE` (inline JSON) and
`AMGPT_INGRESS_TRUST_SHA256`. `AMGPT_AUTH_ISSUER` is also the issuer pin.
`AMGPT_RUNTIME_GATEWAY_BASE_URL` is the single check-in/join base; backend
`DEVICES__DEVICE_BASE_URL` must match it. There are no certificate mounts,
public device listeners or published ports. See
[ADR-0005](../decisions/0005-profile-owned-broker-trust.md).

1. Install the Gateway and runtime independently. Bounded Docker observation
   must match the actual allowlisted env values, not only the desired labels.
2. Complete sign-in and manually refresh its status. The client's existing
   `ingress-init` and `ingress-status` step must confirm an enrolled recipient
   with matching thumbprints. After an
   enrollment error, **Refresh sign-in status** must retry without a new login.
3. For local testing, change the owner-approved bundle/pin for the same
   environment and issuer and save the complete profile in Dev menu. Remove
   the Gateway service and reinstall it. Confirm the sibling is untouched,
   Gateway state is clean and fresh DCR/login produces a new device identity.
   State-preserving rotation belongs to the internal reconciliation acceptance
   path rather than explicit uninstall. Dev catalog updates
   require a new client build before reinstallation. There is no separate
   backend-profile apply button; inspection does not apply the selected catalog.
4. Reject a foreign issuer/environment before mutation, malformed/private
   bundle, wrong pin and drift in actual env despite matching labels. If a
   mutation outcome is unknown, retain the saved service and verify remote
   state before retrying; never report it as confirmed. Core reconciliation
   tests separately cover same-environment rotation for future integration.
5. **Requires workload #25:** independently confirm that the process loaded
   inline trust and verifies grants without TLS-listener inputs. A matching
   Docker env proves delivery only. Then pair with backend #578: healthy
   check-in, broker `joined`, real Codex answer, second turn and a tool call.
   If inline consumption/dial-out is absent, record these steps as unavailable;
   client unit tests or a healthy container do not establish broker E2E.

## Workload-announced web interface

The client reads the installed Device Gateway's owner-only management command
once when its management page is entered or **Refresh state** is selected:

```sh
timeout 10s sudo -n docker exec -i amnezia-amgpt-device-gateway workloadctl links --json
```

Sign-in status retrieval does not cause another links read. There is no
polling timer or automatic browser opening. A successful response with one valid
`client-launch:v1` relation and `text/html` media type enables **Open WebUI**.
Clicking it reads the command again and opens the freshly returned complete
`href`; the page retains only availability. The client does not construct routes from
a device ID, resolve a WebUI origin from backend profiles, or contact backend
discovery. Local/dev behavior comes from the workload's announced link.

A successful `{"schema_version":1,"links":[]}` is normal absence: hide the
button without a reason. No links state machine or extra login/connectivity
status requests are used. Exit 1 returns
`{"schema_version":1,"error_code":"gateway-unavailable"}` without `links`:
it is command failure, never empty success, even if the exit status is wrong.
Exit 2 is unsupported/invalid invocation; failures and malformed responses
hide the action without retries. A user activation that fails shows a generic
navigation error and opens no browser.

Since the SSH abstraction does not expose process exit status, the client
reads a terminal exit-status marker from its shell wrapper before accepting
JSON. The JSON and marker travel as bounded base64 over SSH to preserve UTF-8
across QString callback chunks. Invalid encoding is rejected. Successful
transport alone cannot enable the action.

Acceptance checks:

1. Announce an HTTPS launch URL with percent-encoded `iss` and
   `target_resource` query parameters. Confirm the button appears and opens
   that URL without reconstructing parameters or assuming a WebUI route.
   Future opaque query parameters and fragments are also accepted.
2. For local, announce `http://localhost:<port>/...`, a 127/8 address or `[::1]`.
   The browser uses the operator's machine, not the Docker host.
3. Remove the announcement, logout, restart the Gateway or return invalid
   links. Refresh state; the old button must disappear. A change between page
   entry and clicking must be detected by the second command read.
4. Try an older image, unavailable management socket and malformed JSON.
   Confirm no browser opening, retry loop or retained URL. Back/navigation and
   the authorization-page button remain usable while SSH is in flight.

The parser rejects userinfo (including empty userinfo), backslashes,
whitespace/control characters, unsafe schemes, remote HTTP, duplicate JSON
members and URLs over 2048 UTF-8 bytes. The response is bounded to 8192 bytes.
A nonempty successful document contains exactly one `{rel,type,href}` record with
`rel: urn:amnezia-gpt:link-relation:client-launch:v1` and `type: text/html`.
Query and fragment are opaque; literal `@` in a path/query is allowed, but
userinfo in the authority is not. Unknown relations or media types do not
acquire browser semantics; legacy `{kind,href}` records are rejected. URLs are
never persisted or logged by this feature. See [ADR-0006](../decisions/0006-workload-announced-browser-links.md).

This requires a Gateway containing `links-v1` (workload #31) and a backend
announcing links (router #604). Client tests prove the parsing, SSH and UI
boundaries only; an empty list before router rollout is not an E2E failure
or evidence of a working WebUI connection. Update both client and Gateway
to the simplified development-only v1; prior responses containing `state`
are deliberately rejected.
