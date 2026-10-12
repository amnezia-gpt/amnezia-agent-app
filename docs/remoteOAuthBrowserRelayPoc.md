# Self-hosted agent workloads and device-login PoC

## Status and scope

The fork deploys two independent Linux/amd64 workloads through the existing
Amnezia SSH pipeline: `openclaw-codex` and `amgpt-device-gateway`. The Device
Gateway owns AMGPT device-login presentation and management. A disposable-host
integration run is still required before a new image pair is considered proven
end to end. This is not an upstream VPN change.
The native Codex deployment and Gateway-owned capability are defined in
[ADR-0007](decisions/0007-gateway-owned-codex-capability.md). The client stores
no App Server token or digest; both install orders are supported.

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
  and the remote Codex session. Amnezia never copies the remote home.

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
validated HTTPS Auth, Router and Runtime Gateway coordinates plus the exact
public signing export and SHA-256 pin in the Dev console. Use **Paste public
signing bundle** so the preview cannot normalize the stored bytes. Profile
values remain in local
settings/backups and deployment configuration, not in published source.
New Device Gateway installations snapshot the complete selected backend profile.
Shared dev coordinates are listed below. Its compiled v3 catalog includes the
environment owner's public signing bundle with key
`ingress-1db2aedb3e974f7fbe2d23d006f951ed` and the SHA-256 of its exact UTF-8
serialization, including the final LF. A contract test validates the actual
embedded profile through the production deployment validator.

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

Inspection, login and stop operations use that saved snapshot. Removal uses
only the canonical workload/container identity and observed ownership, so a
legacy or invalid trust profile cannot block cleanup. The client clears the
saved service entry only after verifying that the remote container and its
private state volumes are absent; unknown outcomes retain it for retry.
Explicit removal deletes the selected workload's private state, login and
identity, including leftover volumes when its container is already absent.
Shared digest-volume/network resources remain while any container uses them; the
selected image reference is removed when unused. The sibling runtime remains.
Internal reconciliation can consume the latest
selected profile for the same environment and issuer while retaining Gateway
state/login. There is no separate profile-apply control in the workload UI.
For local testing, save the profile in Dev menu, remove the Gateway service
and reinstall it; dev profile updates require a new client build. Switching
the setting has no remote effect; a different environment or issuer requires
an independent installation. Production profile delivery remains deferred to
integration with Amnezia's existing configuration-delivery mechanism.
See [ADR-0005](decisions/0005-profile-owned-broker-trust.md) for exact env names,
validation, observed configuration and remaining workload/backend gates.

1. The user independently installs each selected service. Neither publishes a host port.
2. Amnezia waits for apply and health verification and displays the outcome.
3. On a healthy, converged Device Gateway page, the user chooses **Sign in with
   Amnezia GPT**. Login is separate from deployment. The Codex page has no
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
7. The SSH interaction ends. The Device Gateway continues polling.
   The user approves in the browser; credentials remain in workload volumes.
8. After approval, the user explicitly chooses **Refresh Amnezia GPT sign-in
   status**, including after reopening the page. The client schedules no
   authorization-status polling. SSH runs asynchronously; only additional SSH
   actions are disabled while a request is in flight. Browser opening and back
   navigation remain available. A failed status request preserves the pending
   verification link and user code; a confirmed terminal status clears them.

   ```sh
   workloadctl login status amgpt --json
   ```

9. When status is `ready`, the client completes device access setup before
   displaying **Signed in and ready**. Through the same owned, converged Gateway
   SSH path it runs `amgpt-device-gateway manage ingress-init`, then
   `amgpt-device-gateway manage ingress-status`. Both replies must be bounded
   JSON with state `enrolled` and the same canonical SHA-256 public key
   thumbprint. Enrollment is idempotent; refreshing status retries it without
   requiring a new sign-in. Failure, malformed output, or a changed recipient
   produces a separate device-access setup error and never a ready result.
   Gateway retains the recipient private key in its state volume and enrolls
   only its public key with Auth after owner activation. The client never reads
   the key or device tokens. This is distinct from DCR and preserves device ID.

After approval, Device Gateway owns automatic check-in using its device-flow
access token. With the Runtime Gateway coordinate configured, device login
requests `device.connectivity:create`. Check-in begins when Codex App Server
is reachable over its authenticated private WebSocket listener, regardless of
whether the desktop remains open. No separate client command or check-in UI is needed.
Authorization `ready` is compute status, not proof of successful connectivity;
manual acceptance reads `workloadctl connectivity status --json` and expects
`active`. Broker session dial-out remains workload-owned. Browser entry points
are read separately through `workloadctl links --json`; see
[ADR-0006](decisions/0006-workload-announced-browser-links.md). The client
retains no WebUI origin or device-route template in backend profiles.

Start and status commands have remote time limits of 95 and 10 seconds,
respectively, in addition to existing SSH transport handling. Transport failure
does not prove that a login grant was never created: refresh status or retry
the workload's idempotent start. No client-side device-token polling is added.
Recipient initialization and verification have remote time limits of 25 and
10 seconds. A lost connection may leave enrollment complete on the remote;
refreshing status safely resumes verification. Raw management output and key
thumbprints are not exposed in UI messages or logs. Recipient enrollment alone
does not provide broker session dial-out or install the backend signing bundle.

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
