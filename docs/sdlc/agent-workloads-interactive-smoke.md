# Interactive OpenClaw and Codex smoke commands

Use this runbook first when the operator asks for SSH test requests or live
agent output. Give the commands immediately. Rediscover flags/configuration only
after a failure or a runtime version change. Do not run billed inference just to
prepare command instructions.

## Known runtime contract

The workload image pins OpenClaw 2026.7.1 and Codex CLI 0.144.3. These are
command instructions, not evidence that an inference turn has completed on any
particular deployment. Do not add operator-specific smoke evidence here.

- Runtime container: `amnezia-openclaw-codex`.
- Gateway container: `amnezia-amgpt-device-gateway`.
- OpenClaw identity: `openclaw`, workspace `/workspace`; image defaults already
  select its HOME, state directory and rendered config.
- Codex identity: `codex`, HOME `/home/codex`, CODEX_HOME `/home/codex/.codex`,
  workspace `/codex-workspace`. Set these explicitly for `docker exec`.
- Managed Codex overrides: `/opt/workload/config/codex-app-server-overrides.json`.
  Reuse them rather than reconstructing provider flags. They select the workload
  Gateway, model catalog, model and container sandbox policy.
- The overrides require `AMGPT_CODEX_USER_AGENT=Codex/<installed-version>`.
  Neither command requires copying OAuth tokens or API keys.
- Do not run service commands as container root: service-owned homes are private
  and root lacks DAC override capabilities.
- `openclaw agent --json` buffers its final result. For a visible interactive
  run, use TUI. `codex exec` streams progress to stderr; inherit both stdout and
  stderr instead of capturing them until process exit.

## One-shot SSH commands (default)

Default to one command per SSH connection: stream stdout/stderr and disconnect
when it completes. Give copy/paste commands on one physical line with balanced
quotes; avoid multi-line quoted SSH payloads. If a shell is awaiting more input,
Ctrl+C cancels that incomplete input. Do not require a persistent SSH shell or TUI unless requested.
Substitute the operator's connection coordinates without storing them here.

OpenClaw: use a temporary TTY (`ssh -t` and `docker exec -it`) and explicit
`--log-level info` plus `--verbose on`. The remote command exits on completion;
no persistent SSH shell or TUI is started. Keep one physical line per command.

First turn:

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker exec -it -u openclaw amnezia-openclaw-codex openclaw --log-level info agent --local --agent main --session-id smoke-1 --verbose on --timeout 180 --message "Reply with exactly SMOKE_OK."'
```

Second turn with the same agent conversation:

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker exec -it -u openclaw amnezia-openclaw-codex openclaw --log-level info agent --local --agent main --session-id smoke-1 --verbose on --timeout 180 --message "What exact phrase did I ask you to reply with in my previous message?"'
```

Tool execution in a separate conversation:

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker exec -it -u openclaw amnezia-openclaw-codex openclaw --log-level info agent --local --agent main --session-id smoke-2 --verbose on --timeout 180 --message "Run uname -a using your shell tool and explain its output."'
```

Free-form question:

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker exec -it -u openclaw amnezia-openclaw-codex openclaw --log-level info agent --local --agent main --session-id smoke-3 --verbose on --timeout 180 --message "Explain Docker briefly in two sentences."'
```

The session ID selects agent conversation history, not a persistent SSH session.
CLI verbose output is not a token-by-token response stream. Keep stderr visible;
do not pipe it into a final-result-only parser.

Codex progress and tool output, using the managed profile:

```sh
ssh -T -p <ssh-port> -i <private-key> <ssh-user>@<host> \
  'docker exec -i -u codex -w /codex-workspace -e HOME=/home/codex -e CODEX_HOME=/home/codex/.codex amnezia-openclaw-codex node' <<'JS'
const fs = require("node:fs");
const { spawnSync } = require("node:child_process");
const version = spawnSync("codex", ["--version"], { encoding: "utf8" }).stdout.trim();
const match = /^codex-cli (\S+)$/.exec(version);
if (!match) throw new Error("Cannot determine installed Codex version");
process.env.AMGPT_CODEX_USER_AGENT = "Codex/" + match[1];
const overrides = JSON.parse(fs.readFileSync("/opt/workload/config/codex-app-server-overrides.json", "utf8"));
const args = overrides.flatMap(value => ["-c", value]);
args.push("exec", "--ignore-user-config", "--skip-git-repo-check", "--color", "never",
  "First explain your next step, then run pwd and printf CODEX_OK using your shell tool, and explain the result.");
process.exit(spawnSync("codex", args, { stdio: "inherit" }).status ?? 1);
JS
```

## Watch Device Gateway in a second terminal

While an agent turn runs in the first terminal, follow Gateway diagnostics in
another terminal with this one-line command:

```sh
ssh -t -i <private-key> -o IdentitiesOnly=yes -p <ssh-port> <ssh-user>@<host> 'docker logs -f --tail 20 amnezia-amgpt-device-gateway'
```

This command deliberately stays connected to follow new events. Ctrl+C stops
the log viewer. Gateway logs contain bounded diagnostic outcomes, not raw
model prompts, responses or credentials. Watching them is not evidence of a
successful inference turn; inspect the agent's own result as well.

## Persistent SSH connection (only when requested)

Use the operator's own host, account, port and authentication method. Do not
commit connection details, credentials or private key paths into this runbook.

```sh
ssh -t -p <ssh-port> <ssh-user>@<host>
```

Run the following blocks inside that SSH session.

## OpenClaw: live terminal UI

```sh
docker exec -it -u openclaw -w /workspace -e TERM=xterm-256color \
  amnezia-openclaw-codex \
  openclaw tui --local --session ssh-openclaw-smoke --timeout-ms 180000 \
  --message 'First explain your next step, then run pwd and printf OPENCLAW_OK using your shell tool, and explain the result. Do not read secrets or modify files.'
```

Use Ctrl+O to expand tool output, Ctrl+T to toggle available thinking output,
Esc to abort a running turn, and Ctrl+D to exit. The model's internal reasoning
may not be exposed; visible progress consists of messages, tools and status.
Use a different session name for a fresh conversation. Check state-directory
ownership behavior when upgrading OpenClaw; newer versions may require using
the running OpenClaw Gateway rather than local embedded mode.

## Codex: live progress and command output

The Node launcher uses built-in modules and inherits the terminal streams.
It copies the same managed overrides as the supervised App Server and derives
the required runtime header from the bundled binary.

```sh
docker exec -it -u codex -w /codex-workspace \
  -e HOME=/home/codex -e CODEX_HOME=/home/codex/.codex \
  amnezia-openclaw-codex node -e '
const fs = require("node:fs");
const { spawnSync } = require("node:child_process");
const version = spawnSync("codex", ["--version"], { encoding: "utf8" }).stdout.trim();
const match = /^codex-cli (\S+)$/.exec(version);
if (!match) throw new Error("Cannot determine installed Codex version");
process.env.AMGPT_CODEX_USER_AGENT = "Codex/" + match[1];
const overrides = JSON.parse(fs.readFileSync("/opt/workload/config/codex-app-server-overrides.json", "utf8"));
const args = overrides.flatMap(value => ["-c", value]);
args.push("exec", "--ignore-user-config", "--skip-git-repo-check", "--color", "always",
  "First explain your next step, then run pwd and printf CODEX_OK using your shell tool, and explain the result. Do not read secrets or modify files.");
process.exit(spawnSync("codex", args, { stdio: "inherit" }).status ?? 1);
'
```

Ctrl+C interrupts the foreground test. This creates a separate CLI session;
it does not send a turn into an existing supervised App Server thread.

## Runtime prerequisites

- Both workloads must be running. Sign-in readiness alone does not prove
  automatic check-in; the shared Codex socket must also be ready.
- `docker stop` leaves a workload installed. Removing a server from the desktop
  leaves its services on the host. Do not interpret either operation as remote
  workload removal.
- Check-in without credentials should return the Runtime service's JSON 401;
  this proves route reachability, not authenticated check-in success.

References: [OpenClaw TUI](https://docs.openclaw.ai/web/tui) and
[OpenAI Docs: non-interactive Codex](https://learn.chatgpt.com/docs/non-interactive-mode).
