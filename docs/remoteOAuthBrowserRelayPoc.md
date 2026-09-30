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
- Amnezia selects an immutable publication, resolves the Auth/Router profile as one tuple,
  declares desired state, observes containers, and applies explicit changes.
- Embedded scripts execute that declaration over SSH.
- Workloads own provider polling, token exchange, credential storage, refresh,
  and the remote Codex/OpenClaw session. Amnezia never copies the remote home.

The two services remain independent in the client. Installing, stopping,
repairing or removing one must not implicitly mutate the other. Both join
`amnezia-agent-workloads`, the existing shared Docker network.
See [ADR-0001](decisions/0001-agent-workload-desired-state.md) for reconciliation.

## Development image contract

OpenClaw/Codex remains pinned to its immutable publication. Until registry pull
access is enabled for the renamed Device Gateway repository, the Device Gateway
payload is published under the legacy image repository and its moving `dev`
tag. This compatibility repository name does not change the workload identity:

```text
docker.io/amneziavpn/agent-workload-openclaw-codex@sha256:f4c7c57e13ec09b75669e0ed15bc6e81909c202159a3979c529ef30bd53d517b
docker.io/amneziavpn/agent-workload-amgpt-auth-proxy:dev
```

Only `linux/amd64` is supported by these images. The client build path uses
`docker build --no-cache --pull`, so a fresh Device Gateway install resolves the
current `dev` manifest without requiring a client rebuild.

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
validated HTTPS pair in the Dev console. Endpoint values remain in local
settings/backups and deployment configuration, not in published source.
New Device Gateway installations snapshot the complete selected backend profile.
Inspection, login, repair and lifecycle operations use that saved snapshot,
not the current global selection. Switching the setting has no remote effect;
switching an installed backend requires an explicit new installation.

1. The user independently installs each selected service and chooses its port.
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
