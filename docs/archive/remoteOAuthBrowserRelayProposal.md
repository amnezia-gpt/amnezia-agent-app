# Historical proposal: OpenClaw Codex Runtime and Remote OAuth Browser Relay

> Superseded by [the detached device-login design](../remoteOAuthBrowserRelayPoc.md)
> and [ADR-0002](../decisions/0002-detached-workload-device-login.md).
> The remainder records an earlier proposal, not the implemented runtime contract.

## Status

This document describes a local proof of concept built on top of the `dev`
branch. It is experimental and is not intended to be merged into upstream
`dev` in its current form.

The deployed workload is OpenClaw with its native Codex app-server harness.
OpenClaw initiates OAuth, owns the resulting credential profile, and injects
the current credential into an unmodified official Codex app-server through
its auth bridge. Amnezia deploys the workload and relays the interactive
browser flow; it does not implement an identity provider and does not persist
OAuth credentials.

## Initial milestone

The first milestone ends when a remote Docker workload has all of the
following properties:

1. OpenClaw starts successfully.
2. The OpenClaw-maintained `@openclaw/codex` plugin is installed and enabled.
3. An OpenAI model is pinned to the native `codex` agent runtime.
4. OpenClaw completes its Codex-compatible browser login through Amnezia.
5. OpenClaw persists a live per-user OpenAI OAuth profile in its agent auth
   store.
6. OpenClaw injects that profile into the official Codex app-server and can run
   the selected model through runtime `codex`.
7. The credential and runtime state survive an OpenClaw and container restart.

Telegram, other channels, and the final mechanism for submitting user input to
the running agent are outside this milestone.

## Why the native Codex harness

OpenClaw exposes two different ways to run Codex:

- the native `@openclaw/codex` harness, which starts the official Codex
  app-server and preserves Codex threads, tools, compaction, and app-server
  semantics;
- the generic ACPX adapter, which exposes Codex as an external ACP agent.

The native harness is the correct choice for this proof of concept. ACPX adds
an unnecessary protocol layer and is intended for deployments that explicitly
need ACP behavior. It is not required merely to make Codex the OpenClaw agent
runtime.

At the inspected OpenClaw revision, the Codex plugin pins the official
`@openai/codex` package at version `0.148.0` and manages the app-server binary
it starts. A globally installed `codex` executable does not replace that
managed runtime during normal OpenClaw startup.

## Authentication ownership decision

The PoC uses the default agent-scoped Codex home and an OpenClaw-managed OAuth
profile.

OpenClaw performs the Codex-compatible OAuth flow itself. It generates state
and PKCE material, receives the authorization callback, exchanges the code,
stores the access and refresh tokens in the selected OpenClaw agent auth store,
and refreshes the profile when required. The OpenAI provider currently uses
the same public OAuth client ID as Codex and identifies the flow with
`originator=openclaw`.

When the native Codex harness starts, OpenClaw launches the official
plugin-managed Codex app-server in an isolated agent-scoped home. It passes the
selected credential through app-server `account/login/start` using ephemeral
Codex credential storage. Codex performs model turns, tools, compaction, and
thread management, but OpenClaw remains the durable OAuth credential owner.

This mode intentionally does not consume a copied or mounted Codex
`auth.json`. A native Codex CLI login is not part of the startup lifecycle.

`appServer.homeScope: "user"` remains a possible alternative if native Codex
credential ownership is required later, but it is not used by this PoC.

## Problem

OpenClaw and Codex run on a remote Linux host inside Docker, while the user
interacts with Amnezia on a local desktop computer.

During browser login, OpenClaw:

1. creates OAuth state and a PKCE verifier/challenge;
2. starts an HTTP callback listener on container loopback;
3. builds an authorization URL whose redirect URI points to
   `http://localhost:<port>/auth/callback`;
4. attempts to open that URL in a browser;
5. validates the callback and exchanges the authorization code for tokens;
6. saves the resulting OAuth profile in the selected agent auth store.

The remote container has no useful graphical browser. Opening the authorization
URL on the desktop is insufficient by itself because `localhost` in the
browser refers to the desktop, while the callback listener belongs to the
remote container.

## Goals

- Deploy OpenClaw with an unmodified official Codex distribution.
- Make Codex the selected OpenClaw agent runtime, not an embedded provider
  implementation or generic ACP process.
- Let OpenClaw own OAuth state, PKCE, token exchange, storage, refresh, and
  logout.
- Open the authorization URL in the user's normal desktop browser.
- Transparently relay the loopback callback to the listener inside the remote
  container.
- Persist the OpenClaw agent auth profile and runtime configuration across
  restarts.
- Ensure Amnezia never stores OAuth codes, access tokens, or refresh tokens.
- Use a mock identity provider first, then validate the same transport against
  the real provider without changing the relay protocol.

## Non-goals

- Telegram or any other OpenClaw channel integration.
- The final user-input transport into OpenClaw or Codex.
- Implementing an OAuth authorization server in Amnezia.
- Replacing the production identity provider.
- Exchanging authorization codes in Amnezia.
- Persisting or refreshing OpenClaw credentials in Amnezia.
- Modifying Codex or maintaining a patched Codex build.
- Routing normal Codex API traffic through Amnezia.
- Using `AmneziaVPN-service` for OAuth processing.
- Designing the final control-plane or node-agent transport.

## Runtime configuration

The PoC configuration must explicitly enable the plugin and pin the model to
the Codex harness. Relying on runtime auto-detection would make a successful
start ambiguous.

Illustrative OpenClaw configuration:

```json5
{
  plugins: {
    entries: {
      codex: {
        enabled: true,
        config: {
          appServer: {
            transport: "stdio",
            homeScope: "agent",
            clearEnv: ["CODEX_API_KEY", "OPENAI_API_KEY"],
          },
          sessionCatalog: {
            enabled: false,
          },
        },
      },
    },
  },
  agents: {
    defaults: {
      model: { primary: "openai/gpt-5.6-sol" },
      models: {
        "openai/gpt-5.6-sol": {
          agentRuntime: { id: "codex" },
        },
      },
    },
  },
}
```

The exact model may change, but `agentRuntime.id: "codex"` must remain
explicit. `sessionCatalog` is disabled for the first milestone because native
session browsing is not required yet.

The local OAuth path uses OpenClaw's `openUrl` callback. On Linux this resolves
to `xdg-open`; it does not consult `$BROWSER`. For the initial bridge spike,
the container environment includes:

```text
PATH=/opt/amnezia/bin:<normal-runtime-path>
DISPLAY=:0
```

`/opt/amnezia/bin/xdg-open` is the browser bridge. The synthetic display value
only selects OpenClaw's local browser-opening path; the bridge does not connect
to an X server. The container process must not inherit `SSH_CLIENT`, `SSH_TTY`,
or `SSH_CONNECTION`, because those variables make OpenClaw select its remote
manual-code flow. `REMOTE_CONTAINERS` and `CODESPACES` must also be unset.

The inspected OpenClaw implementation writes the complete authorization URL to
its runtime log after invoking the local opener, and also prints it in the
remote/manual flow. The bridge spike can prove transport with ephemeral test
logging, but the integrated PoC must add a small OpenClaw change that emits a
structured browser-open event to the sidecar and suppresses the raw URL log.
Parsing stdout is prohibited. The preferred upstreamable change is an explicit
external URL-opener hook with redacted status logging; the `xdg-open` wrapper
remains a fallback implementation.

`CODEX_API_KEY` and `OPENAI_API_KEY` must not be inherited by the app-server.
Otherwise an ambient key can make an authentication failure look like a
successful user login.

The Codex app-server command remains managed by the OpenClaw plugin. The PoC
does not install or invoke a separate Codex CLI for login.

## Deployed components

### OpenClaw runtime

The long-lived process that owns the agent configuration and invokes turns
through the native Codex harness.

### Native Codex plugin

The OpenClaw-maintained `@openclaw/codex` plugin. It launches the official
Codex app-server over stdio and exposes the `codex` runtime to OpenClaw.

### OpenClaw authentication command

A short-lived interactive command such as:

```text
openclaw models auth login --provider openai --agent <agent-id>
```

It runs the OpenClaw OpenAI OAuth implementation and writes the resulting
profile into that agent's persistent auth store.

### Browser bridge

A small executable installed as the container's selected `xdg-open` command.
It receives the authorization URL from OpenClaw's `openUrl` path and sends a
browser-open request to the relay sidecar. It never logs the URL.

### Relay sidecar

A process deployed in the same network namespace as OpenClaw. It maintains a
control connection to Amnezia and can reach OpenClaw's loopback callback
listener.

### Desktop relay

A user-space component inside Amnezia. It binds the requested desktop loopback
port, opens the system browser, and tunnels raw TCP streams to the remote relay
sidecar.

## Architecture

```text
Remote host                                         User desktop

+-----------------------------------------+         +------------------------+
| OpenClaw workload container             |         | Amnezia application    |
|                                         |         |                        |
| persistent OpenClaw agent state         |         | desktop relay          |
|       ^                                 |         |   |                    |
|       |                                 |         |   | open system browser|
| OpenClaw OAuth flow                     |         |   v                    |
|       |                                 |         | desktop browser        |
|       | xdg-open <authorization URL>    |   SSH   |   |                    |
|       v                                 | tunnel  |   | localhost callback |
| browser bridge -> relay sidecar         |<------->|   v                    |
|       |                                 |         | temporary loopback     |
|       v                                 |         | listener               |
| 127.0.0.1:1455 OpenClaw listener        |         +------------------------+
|                                         |
| OpenClaw auth store                     |
|       | account/login/start             |
|       v                                 |
| @openclaw/codex -> official app-server  |
+-----------------------------------------+
```

This is a remote browser bridge and loopback callback relay. It is not a TLS
man-in-the-middle proxy and does not impersonate the identity provider.

## Deployment and startup lifecycle

Amnezia already performs self-hosted installation from the desktop process:

1. The UI produces a typed installation configuration.
2. `InstallController` creates an SSH session.
3. Docker is installed or validated on the target host.
4. Dockerfiles and setup scripts are uploaded through SCP.
5. The image is built and the workload is started through SSH commands.

The PoC extends that path with one OpenClaw workload. The simplest topology is
one container containing OpenClaw, the native Codex plugin, browser bridge, and
relay sidecar. A separate relay container is acceptable only if it joins the
exact network namespace containing the OpenClaw OAuth listener.

The startup state machine is:

```text
installed
  -> configured
  -> authentication check
       -> authenticated -> OpenClaw ready
       -> login required -> login in progress
            -> authenticated -> OpenClaw ready
            -> failed or cancelled -> login required
```

Recommended sequence:

1. Mount the persistent OpenClaw state, agent auth store, and workspace
   volumes.
2. Install or verify the pinned OpenClaw-maintained Codex plugin.
3. Write the fail-closed runtime configuration.
4. Start the OpenClaw Gateway and relay sidecar without marking the workload
   ready for model turns.
5. Inspect the target agent with
   `openclaw models auth list --provider openai --agent <agent-id> --json`.
6. If no usable profile exists, run
   `openclaw models auth login --provider openai --agent <agent-id>` through an
   interactive bootstrap channel.
7. After successful login, verify that the target agent has a usable OAuth
   profile without printing credential material.
8. Start a native Codex harness probe and verify that app-server
   `account/read` reports the injected ChatGPT account.
9. Mark the workload ready only after both OpenClaw and the native Codex
   account checks succeed.

Only one OAuth flow may mutate a given agent auth profile at a time. Re-login,
logout, refresh, and model turns must use OpenClaw's auth-store coordination
rather than editing credential files directly.

## Login flow

### 1. OpenClaw starts the flow

The OpenClaw OpenAI provider generates OAuth state and PKCE material, binds its
loopback callback listener, and constructs the authorization URL. The current
implementation uses `http://localhost:1455/auth/callback`, the public Codex
OAuth client ID, and `originator=openclaw`. The relay must still derive the
host, port, and path from the actual authorization URL rather than hard-coding
them into the desktop application.

### 2. The browser bridge captures the URL

When OpenClaw invokes `xdg-open`, the bridge receives the complete authorization
URL. It validates that:

- the authorization URL uses HTTPS;
- the authorization host is allowed by runtime configuration;
- the redirect URI uses an allowed loopback host;
- the callback path and port are valid for the active flow.

It sends a `browser.open` request to the sidecar over a local Unix socket. The
URL must not be written to logs or command output.

### 3. Amnezia prepares the desktop callback

The sidecar assigns an opaque flow ID and forwards the request over the
authenticated control channel. Amnezia extracts the loopback port, binds the
desktop listener first, and only then opens the browser.

If the port is unavailable on the desktop, the flow fails. Amnezia cannot
silently select another port because the redirect URI is already part of the
authorization request and PKCE flow.

### 4. The browser authenticates directly with the provider

Amnezia opens the unmodified authorization URL in the system browser. Login
pages, cookies, passwords, and MFA remain between the browser and provider.

### 5. The callback is relayed transparently

The provider redirects the browser to a URI such as:

```text
http://localhost:1455/auth/callback?code=<opaque>&state=<opaque>
```

The relays copy bytes in both directions:

```text
browser <-> desktop loopback listener <-> SSH stream
        <-> relay sidecar <-> OpenClaw loopback listener
```

The relay does not parse, validate, rewrite, or persist the authorization code,
state, headers, response body, or redirects.

### 6. OpenClaw completes and persists authentication

OpenClaw validates state, uses its PKCE verifier, calls the token endpoint
directly from the remote runtime, and saves the access and refresh credentials
in the selected agent auth store. The relay is no longer involved after the
browser flow ends.

For a Codex model turn, OpenClaw resolves the selected OAuth profile, refreshes
it when required, launches the official Codex app-server, and passes the
current credential through app-server `account/login/start`. The app-server
uses ephemeral credential storage; the durable credential remains in OpenClaw.

## Relay transport

The PoC needs a multiplexed, bidirectional protocol over one authenticated SSH
session. Illustrative messages are:

```text
browser.open { flow_id, authorization_url, callback_host, callback_port }
browser.ready { flow_id }
browser.failed { flow_id, reason_code }
stream.open { flow_id, stream_id }
stream.data { stream_id, bytes }
stream.half_close { stream_id }
stream.close { stream_id }
flow.cancel { flow_id }
flow.complete { flow_id }
```

A length-prefixed binary framing format is preferable to newline-delimited
messages because HTTP stream data is arbitrary binary data. Flow and stream
IDs are opaque random values and must not contain OAuth parameters.

The current `SshSession::runScript` and `libssh::Client::executeCommand` APIs
are designed for finite commands and are unsuitable for this protocol. The PoC
requires a persistent SSH channel abstraction with concurrent reads and
writes, logical stream multiplexing, cancellation, timeouts, bounded buffers,
and backpressure.

## Mock identity-provider phase

The first transport test uses a local mock OAuth/OIDC provider. It is a test
fixture, not part of the production architecture. It must provide:

- an authorization endpoint;
- an authorization-code response containing the original state;
- PKCE S256 validation;
- single-use, short-lived authorization codes;
- deterministic success, denial, and malformed-callback cases;
- token and refresh responses sufficient for the fixture to complete login.

Unlike stock Codex CLI, the inspected OpenClaw OAuth implementation currently
hard-codes the authorization endpoint, token endpoint, client ID, and callback
port. It does not expose Codex's `--experimental_issuer` and
`--experimental-client-id` flags.

Therefore the first mock phase uses a standalone OAuth flow fixture that calls
the exact same `xdg-open` bridge and loopback relay contract. It proves the
transport independently of OpenAI. A later test may add an explicitly
test-only issuer injection point to the local OpenClaw PoC branch, but that
override must never be enabled in the production workload.

The mock phase proves that the bridge captures the URL, Amnezia opens the
desktop browser, and the callback reaches a remote loopback listener. Real
OpenClaw validation then proves its own state, PKCE, token exchange, auth-store
persistence, and Codex app-server injection without DNS rewriting, a custom
CA, TLS interception, or a token proxy.

## Persistence and secrets

The deployment needs separate durable mounts for:

- OpenClaw state, agent configuration, and agent auth stores;
- the agent workspace.

OpenClaw state must be writable only by the unprivileged container runtime
user. Auth files must not be copied into the image, shell templates, Docker
build layers, environment files, Amnezia settings, or diagnostics.

The browser relay treats the authorization URL and callback bytes as transient
sensitive data. They remain memory-only and are discarded when the flow
finishes, fails, times out, or disconnects. Before real-provider validation,
OpenClaw's current raw authorization-URL logging must be removed or redacted so
the same no-logging rule holds across the entire workload.

## Security requirements

### SSH server authentication

Amnezia must verify or pin the remote SSH host key before transporting browser
relay traffic. The current self-hosted SSH implementation does not expose an
obvious host-key verification step; this must be addressed before the PoC is
treated as security-relevant.

### Loopback exposure

- Desktop and remote listeners bind only to loopback interfaces.
- The callback port is never published through Docker.
- A relay accepts streams only for an active flow and exact expected port.
- Listeners are removed on completion, cancellation, timeout, SSH disconnect,
  or application shutdown.

### URL validation

The bridge and desktop relay reject unexpected schemes, provider hosts,
redirect hosts, callback paths, and ports. This prevents a compromised remote
process from turning Amnezia into a general-purpose browser opener or TCP
forwarder.

### Flow isolation

Every request has a unique flow ID bound to the selected host, deployed
workload, OpenClaw auth process, and callback port. A callback from one flow
must never be delivered to another runtime.

## Failure handling

The PoC must handle at least:

- desktop callback port already in use;
- OpenClaw callback port `1455` already in use;
- browser bridge or relay unavailability;
- SSH loss before or during callback;
- user cancellation or provider denial;
- login timeout or OpenClaw auth-command exit;
- malformed authorization URL;
- callback connection without an active flow;
- a model turn starting before OpenClaw login completes;
- corrupted, missing, or incorrectly owned OpenClaw auth state;
- container restart during login.

Failures return the workload to `login required`. The relay must never replay a
captured callback or synthesize an OAuth result.

## Chosen implementation process

The PoC is implemented as a sequence of vertical slices. The Amnezia client is
not changed until the container runtime, sidecar contract, and local relay have
already been proven independently. This prevents Docker, OpenClaw, OAuth, SSH,
and UI failures from being debugged at the same time.

The fixed order is:

```text
container runtime
  -> sidecar protocol
  -> local desktop-relay simulator
  -> mock OAuth transport
  -> persistent SSH channel in Amnezia
  -> existing SSH Docker injection
  -> Amnezia login UI
  -> real OpenAI OAuth
  -> native Codex runtime probe
```

### Phase 1: Local container runtime

Build a reproducible local container containing:

- OpenClaw;
- the OpenClaw-maintained `@openclaw/codex` plugin;
- the plugin-managed official Codex app-server;
- the relay sidecar;
- the browser-opener bridge;
- persistent OpenClaw state and workspace mounts.

Pin `openai/gpt-5.6-sol` to `agentRuntime.id: "codex"` and use
`appServer.homeScope: "agent"`. Confirm that OpenClaw and the Codex harness
start, but do not integrate Amnezia or the real provider yet.

The phase is complete when the container can be created, inspected, stopped,
started again, and reports deterministic runtime and authentication states.

### Phase 2: Sidecar contract and local relay

Implement the sidecar control surface before implementing its SSH transport.
The minimum operations are:

```text
runtime.status
auth.status
auth.start
auth.cancel
browser.open
browser.ready
browser.failed
stream.open
stream.data
stream.half_close
stream.close
auth.completed
auth.failed
```

`auth.start` makes the sidecar start the interactive OpenClaw auth command. The
Amnezia client must never manage the command's terminal conversation directly.
The opener bridge sends the authorization URL to the sidecar over a local Unix
socket.

Create a host-side desktop-relay simulator and a standalone OAuth fixture. Use
them to prove browser-open signaling and transparent callback forwarding
without SSH and without modifying the Amnezia client.

The phase is complete when the mock authorization callback crosses the host to
container boundary, reaches the fixture's loopback listener, and all success,
denial, cancellation, timeout, and port-conflict states are deterministic.

### Phase 3: OpenClaw OAuth integration

Connect the proven sidecar transport to OpenClaw OAuth:

- add a structured external URL-opener hook to the local OpenClaw PoC build;
- remove or redact raw authorization URLs from OpenClaw logs;
- have the sidecar start and supervise
  `openclaw models auth login --provider openai --agent <agent-id>`;
- expose only sanitized auth status to callers;
- prevent concurrent login and logout mutations for one agent profile while
  leaving refresh and model-turn coordination to OpenClaw.

Parsing OpenClaw stdout for authorization URLs is prohibited. The temporary
`xdg-open` wrapper may be used while developing the structured hook, but it is
not the final integration contract.

This phase initially uses the local relay simulator. It is complete when an
OpenClaw auth flow can reach the browser relay contract and the resulting
profile is visible in the selected agent auth store without exposing secrets.

### Phase 4: Persistent SSH transport in Amnezia

Only after the local vertical slice works, add a long-lived SSH channel to the
Amnezia client. Existing finite command execution remains responsible for
installation. The new channel runs a command equivalent to:

```text
docker exec -i <runtime-container> amnezia-openclaw-sidecar stdio
```

The channel carries the sidecar control protocol and multiplexed raw TCP
streams. It must provide:

- SSH host-key verification or pinning;
- concurrent reads and writes;
- bounded buffering and backpressure;
- flow and stream multiplexing;
- cancellation and timeouts;
- disconnect cleanup and explicit reconnect behavior.

Validate this phase with the mock OAuth fixture before using OpenAI.

### Phase 5: Existing SSH Docker injection

Extend the current self-hosted installation path only after the runtime and SSH
channel are independently stable:

```text
InstallController
  -> connect and verify SSH host
  -> upload the PoC build context
  -> build the image on the remote host
  -> create persistent volumes
  -> start the workload
  -> open the sidecar SSH channel
  -> request runtime.status
```

The PoC continues to build the image on the remote host, matching Amnezia's
existing installation model. Publishing and pulling a registry image is a
later supply-chain decision, not part of the initial implementation.

The phase is complete when Amnezia can install, start, inspect, stop, restart,
and remove the workload without initiating OAuth.

### Phase 6: Amnezia login flow

Add the smallest UI and controller surface required to expose these states:

```text
not installed
installed / login required
login in progress
ready
login failed
runtime unavailable
```

The login sequence is fixed:

```text
Amnezia -> auth.start
sidecar -> start OpenClaw auth command
OpenClaw -> structured browser-open hook
sidecar -> browser.open
Amnezia -> bind desktop loopback listener
Amnezia -> browser.ready
Amnezia -> open system browser
browser -> desktop localhost callback
Amnezia -> multiplexed SSH stream
sidecar -> OpenClaw localhost callback
OpenClaw -> token exchange and auth-store persistence
sidecar -> auth.completed
Amnezia -> runtime status probe
```

The desktop listener must bind before the browser opens. Authorization and
callback data remain opaque relay bytes and are never placed in settings,
diagnostics, analytics, or logs.

### Phase 7: Real-provider and Codex validation

After the mock path passes through the real Amnezia SSH transport:

- run OpenClaw OAuth with the normal OpenAI issuer;
- confirm that only browser-open and loopback callback traffic cross Amnezia;
- confirm that token exchange and refresh go directly from OpenClaw to OpenAI;
- restart the workload and confirm the OpenClaw auth profile survives;
- start the native Codex harness and execute one minimal model turn;
- disconnect the browser relay and confirm normal refresh and model use remain
  functional;
- verify that OpenClaw, sidecar, SSH, and Amnezia logs contain no sensitive URL,
  callback, or token material.

Only after this phase should Telegram, other OpenClaw surfaces, or the final
user-input transport be designed.

## Acceptance criteria

The initial proof of concept succeeds when:

1. Amnezia deploys a remote Docker workload containing OpenClaw and the
   OpenClaw-maintained native Codex plugin and official Codex app-server.
2. OpenClaw explicitly resolves the selected model to runtime `codex`.
3. OpenClaw OAuth automatically opens the user's desktop browser.
4. The provider callback reaches the remote OpenClaw loopback listener without
   publishing it publicly.
5. OpenClaw performs state validation, PKCE verification, token exchange,
   token persistence, and refresh.
6. OpenClaw injects the selected credential into the official Codex app-server,
   which reports an authenticated ChatGPT account.
7. The OpenClaw auth profile and runtime configuration survive workload
   restart.
8. Removing the browser relay after login does not affect normal token refresh
   or app-server use.
9. Amnezia logs, settings, diagnostics, and storage contain no authorization
   code, access token, or refresh token.
10. OpenClaw and sidecar logs contain no raw authorization URL or callback
    query data.

## Open questions after the initial milestone

- Which OpenClaw API or channel will submit user input to the authenticated
  Codex runtime.
- Whether the final workload remains one container or separates the relay into
  a shared-network sidecar.
- Whether one SSH channel or several channels should carry multiplexed flows.
- How multiple users map to OpenClaw agents, auth profiles, and isolated Codex
  homes.
- Whether the final control plane replaces the persistent SSH relay with an
  outbound node-agent channel after bootstrap.
