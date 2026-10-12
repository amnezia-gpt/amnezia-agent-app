# Interactive Codex smoke commands

Use this runbook first when the operator asks for SSH test requests or live
agent output. Give ready one-shot commands; rediscover flags only after a
failure or a runtime version change. Do not run billed inference merely to
prepare instructions. Never save operator connection details in this file.

## Native runtime contract (GH-34 / ADR-017)

- Runtime UI name: **Codex**. The compatibility container name remains
  `amnezia-openclaw-codex`; the published image contains no OpenClaw.
- Runtime identity: image-owned `codex` (10100), HOME `/home/codex`,
  CODEX_HOME `/home/codex/.codex`, workspace `/codex-workspace`.
- The inspected native image pins Codex 0.162.1. Its managed provider policy is
  `/etc/codex/requirements.toml`; its model catalog is
  `/opt/codex/config/amgpt-models-app-server.json`. The image supplies
  `AMGPT_CODEX_USER_AGENT`. The old `/opt/workload` override file, `runtimectl`
  and `openclaw` user are gone.
- Gateway: `amnezia-amgpt-device-gateway`. It owns device login, Router
  credentials and the App Server capability token. Do not read or copy those
  credentials into the runtime or desktop.
- The runtime reads only the token digest from the read-only shared volume.
  Only Gateway authenticates to the native App Server. Direct CLI smoke below
  runs a separate Codex process against the managed provider; it does **not**
  prove the Gateway-to-App-Server connection or a WebUI session.

## One-shot SSH commands (default)

Substitute the operator's private key, SSH port, account and host. Each code
block contains one physical command line with balanced quotes. A temporary
TTY streams progress and tool output; the connection closes when the turn
ends. Keep both stdout and stderr visible. Ctrl+C interrupts a foreground turn.

First turn:

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker exec -it -w /codex-workspace amnezia-openclaw-codex codex -c model_catalog_json=\"/opt/codex/config/amgpt-models-app-server.json\" -c model_reasoning_effort=\"low\" exec --skip-git-repo-check --color never --model gpt-5.6-luna "First explain your next step, then run pwd and uname -a using your shell tool, and explain the result. Do not read secrets or modify files."'
```

Continue that CLI conversation (not a persistent SSH connection):

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker exec -it -w /codex-workspace amnezia-openclaw-codex codex -c model_catalog_json=\"/opt/codex/config/amgpt-models-app-server.json\" -c model_reasoning_effort=\"low\" exec --skip-git-repo-check --color never --model gpt-5.6-luna resume --last "What commands did I ask you to run in the previous turn?"'
```

If other CLI tests run concurrently, use the explicit thread ID in place of
`--last`. App Server threads and CLI conversations are separate test surfaces.
The command shape follows the native image policy and earlier CLI smoke;
live CLI inference has not been repeated for this client change.

## Gateway diagnostics in another terminal

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker logs -f --tail 20 amnezia-amgpt-device-gateway'
```

This viewer stays connected; Ctrl+C closes it. Logs contain bounded diagnostic
outcomes, not raw model requests or credentials. The provider proxy does not
emit per-request access logs, and check-in logs reflect transitions rather
than every heartbeat. Silence during a CLI turn does not prove failure.

Read device connectivity without printing credentials:

```sh
ssh -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker exec amnezia-amgpt-device-gateway workloadctl connectivity status --json'
```

## Pair acceptance

Use matching GH-34 / ADR-017 images and client. Remove the previous pair through
Amnezia, then install with fresh volumes and perform a new device login.
Test both install orders. Runtime-first is healthy but locked; creating Gateway
must restart it after publishing the digest. Ordinary Gateway start preserves
its token and does not restart Codex.

- Both containers must be Docker `healthy`; health alone is insufficient.
- Gateway must report `codex_app_auth` with outcome `published`.
- The current runtime start must not report `listener locked until restart`.
  Historical lines from a prior locked start may remain in `docker logs`.
- After login, connectivity must become `active`; open WebUI from the Gateway
  page, start a session and verify streaming output there.
- A failed post-create runtime restart is a partial apply. Restart Codex
  by stopping and starting it through the client and repeat pair acceptance.

Stopping leaves a workload installed. Explicit removal deletes its private
state; shared network/digest resources remain while the sibling uses them.
See [ADR-0007](../decisions/0007-gateway-owned-codex-capability.md) and the
[workload contract](https://github.com/amnezia-gpt/agent-workloads/blob/main/docs/amnezia-client-deployment.md).
