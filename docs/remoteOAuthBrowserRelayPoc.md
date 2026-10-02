# Self-hosted agent workloads and device-login PoC

## Status and scope

The fork deploys two independent Linux/amd64 workloads through the existing
Amnezia SSH pipeline: `openclaw-codex` and `amgpt-device-gateway`. The Device
Gateway owns AMGPT device-login presentation and management. A disposable-host
integration run is still required before a new image pair is considered proven
end to end. This is not an upstream VPN change.

The earlier loopback callback/browser-relay proposal is retained in
[the historical design](archive/remoteOAuthBrowserRelayProposal.md). It is not
the implemented login path. See [ADR-0002](decisions/0002-detached-workload-device-login.md).

## Ownership

- `agent-workloads` publishes images and the versioned runtime contract.
- Amnezia selects workload images and resolves the Auth/Router/Runtime Gateway
  profile as one tuple, declares desired state, observes containers and applies
  explicit changes.
- Embedded scripts execute that declaration over SSH.
- Workloads own provider polling, token exchange, credential storage, refresh,
  and the remote Codex/OpenClaw session. Amnezia never copies the remote home.

The two services remain independent in the client. Installing, stopping,
repairing or removing one must not implicitly mutate the other. Both join
`amnezia-agent-workloads`, the existing shared Docker network.
See [ADR-0001](decisions/0001-agent-workload-desired-state.md) for reconciliation.

## Development image contract

During current local/dev development, both containers use the moving `dev`
channel. The Device Gateway payload is published under the legacy image
repository; this distribution name does not change the workload identity.
The Device Gateway dev image must include the GH-24 automatic check-in contract:

```text
docker.io/amneziavpn/agent-workload-openclaw-codex:dev
docker.io/amneziavpn/agent-workload-amgpt-auth-proxy:dev
```

Only `linux/amd64` is supported by these images. The client apply path pulls
the selected image before `docker run --pull=never`,
so a fresh install resolves the current dev image. This development policy does
not establish an immutable release or production rollout contract.

The canonical deployment contract and `login-start-v1.schema.json` /
`login-status-v1.schema.json` in the `agent-workloads` release define the public
CLI boundary. The client caps responses at 16 KiB, validates schema v1, bounds
verification URLs to 4096 encoded characters and user codes to 256 characters,
and rejects unsafe URL schemes, credentials and fragments. The encoded URL
limit is deliberately conservative for non-ASCII input. Native expiry is
nullable. A failure may have a null mode; it is associated with the mode of
the fixed command that the client issued.

## Deployment and login

The Dev console has an independent **Agent workload environment** selection:
`local` (local backend reached through public tunnels) or `dev` (shared dev,
the default). It does not change the existing Amnezia Gateway environment.
Production coordinates are unapproved and unavailable for new deployments.
Local endpoints have no built-in defaults: the operator enters and saves a
validated HTTPS Auth, Router and Runtime Gateway coordinates in the Dev
console. Endpoint values remain in local
settings/backups and deployment configuration, not in published source.
New Device Gateway installations snapshot the complete selected backend profile.
The shared dev profile is:

| Coordinate | Value |
| --- | --- |
| Auth issuer | `https://agpt-auth-dev.amzsvc.com` |
| Router base URL | `https://agpt-router-dev.amzsvc.com/v1` |
| Runtime Gateway base URL | `https://agpt-runtime-dev.amzsvc.com` |

The Runtime Gateway coordinate is required for new profiles. It is validated
before SSH as absolute HTTPS without userinfo, query, fragment or a trailing
`/v1`, normalized, persisted and included in the canonical specification hash.
It is passed only to Device Gateway as `AMGPT_RUNTIME_GATEWAY_BASE_URL`. The
workload appends `/v1/devices/check-in`; the client never derives this address
from the other two coordinates. Local uses its own operator-supplied URL.

Inspection, login, repair and lifecycle operations use that saved snapshot,
not the current global selection. Switching the setting has no remote effect;
switching an installed backend requires an explicit new installation.

1. The user independently installs each selected service. Neither publishes a host port.
2. Amnezia waits for apply and health verification and displays the outcome.
3. On a healthy, converged Device Gateway page, the user chooses **Sign in with
   Amnezia GPT**. Login is separate from deployment. The OpenClaw page has no
   provider-login controls.
4. AMGPT login requires the Device Gateway itself to be owned, converged and
   live. This is healthcheck liveness, not authenticated readiness.
5. Amnezia invokes a fixed non-TTY command in `amnezia-amgpt-device-gateway`:

   ```sh
   workloadctl login start amgpt --json
   ```

6. One bounded JSON response supplies the verification link and user code.
   The client presents an explicit browser-open action; it does not fabricate
   a complete URI by appending the code to an arbitrary provider URL.
7. The SSH interaction ends. The workload-owned supervisor continues polling.
   The user approves in the browser; credentials remain in workload volumes.
8. The user can refresh AMGPT status, including after reopening the page:

   ```sh
   workloadctl login status amgpt --json
   ```

After approval, Device Gateway owns automatic check-in using its device-flow
access token. With the Runtime Gateway coordinate configured, device login
requests `device.connectivity:create`. Check-in begins when Codex App Server
is reachable through the shared bridge socket, regardless of whether the
desktop remains open. No separate client command or check-in UI is needed.
Authorization `ready` is compute status, not proof of successful connectivity;
manual acceptance reads `workloadctl connectivity status --json` and expects
`active`. Broker session dial-out and WebUI are outside this client change.

Start and status commands have remote time limits of 95 and 10 seconds,
respectively, in addition to existing SSH transport handling. Transport failure
does not prove that a login grant was never created: refresh status or retry
the workload's idempotent start. No client-side device-token polling is added.

Pending presentation lives only in UI memory. Receiving a non-pending status
clears it. A reopened page can query status but does not
recover a URL from the status response; start returns the existing unexpired
presentation when applicable. Container replacement can interrupt a pending
flow; completed credentials live in persistent volumes.

## Security and limitations

SSH/container output is untrusted. Only typed fields reach the UI, user code is
plain text, and browser opening revalidates the HTTPS URL. Raw stdout/stderr,
verification links, codes and credentials must not enter logs or support bundles.
Unknown states or errors fail safely with a generic message.

The client does not install a Gateway on behalf of a login attempt, act as an
identity provider, exchange grants, provide an interactive terminal, or send
agent prompts. Telegram and other runtime surfaces are outside this PoC.
The local privileged VPN service and Apple Developer signing are not required
for the SSH deployment scenario.

## Validation

Local evidence covers parser/security behavior, controller orchestration,
SSH command rendering, resource contracts and the macOS desktop build. It does
not prove real provider approval or remote persistence. Follow the
[disposable-host runbook](sdlc/agent-workloads-dev-smoke.md) to complete GH-1.
Live AMGPT login additionally requires the backend's device authorization
support to be deployed; image publication alone does not establish that.
