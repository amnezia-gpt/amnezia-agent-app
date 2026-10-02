# Agent Guidance

## Repository purpose

This repository is the `amnezia-gpt/amnezia-agent-app` fork of Amnezia VPN.
It remains close to upstream while adding the agent-workload deployment and
browser-mediated authorization proof of concept described in
[`docs/remoteOAuthBrowserRelayPoc.md`](docs/remoteOAuthBrowserRelayPoc.md).

## Start here

1. Inspect the current branch, worktree, remotes, submodules, and affected files.
2. Use `work-intake-and-routing` for new issues or when the implementation path
   is not already explicit.
3. Use `planning-and-task-breakdown` for changes spanning more than one layer,
   platform, repository, or externally visible contract.
4. Use `test-driven-development` for new logic, observable behavior changes,
   and bug fixes. Start with a focused failing test when practical, and use
   characterization tests before refactoring untested legacy behavior.
5. Invoke every repo-local skill whose description matches the work.
6. Preserve unrelated user changes and never overwrite a dirty worktree.
7. For SSH test prompts or live OpenClaw/Codex output, read
   [`docs/sdlc/agent-workloads-interactive-smoke.md`](docs/sdlc/agent-workloads-interactive-smoke.md)
   first and give the ready commands immediately. Do not repeat CLI, container,
   or documentation discovery unless those commands fail or runtime versions change.
   Never add operator-specific connection details or host-specific evidence to
   the public runbook; it contains only the generic workload contract.
   Default to one-shot SSH commands that stream output and disconnect on
   completion. Give copy/paste SSH commands on one physical line with balanced
   quotes. Offer a persistent SSH shell or TUI only when explicitly requested.

## Branch and upstream model

- `upstream/dev` is the upstream Amnezia baseline.
- `main` is the fork synchronization branch and mirrors accepted upstream state.
- `amnezia` is our integration branch.
- Start feature branches from `amnezia` and merge them back into `amnezia`.
- Do not merge feature work directly into `main` or push changes to `upstream`.
- Use `upstream-sync-curator` for synchronization work.

## Project facts

- C++17, CMake 3.25+, Qt 6.10+, and Conan are the native build stack.
- The repository contains four Git submodules under `client/3rd`; initialize
  them before configuring a clean checkout.
- The desktop client, privileged service, embedded server scripts, and
  platform-specific integrations are separate change surfaces.
- GitHub Actions builds Linux, Windows, macOS, macOS Network Extension, iOS,
  and Android. A successful local macOS client build is not evidence for those
  other platforms or for the privileged service.
- The main product currently has no comprehensive first-party automated test
  suite. Do not claim `ctest` coverage based on vendored dependency tests.

## Working rules

- Match existing C++/Qt/QML/shell conventions and `.clang-format`.
- Prefer existing Qt, CMake, and project utilities over new dependencies.
- Treat SSH output, remote files, OAuth parameters, container output, imported
  configuration, and UI input as untrusted data.
- Never log passwords, private keys, authorization codes, tokens, cookies,
  complete callback URLs, or configuration containing credentials.
- Keep remote operations bounded, cancellable where the surrounding API allows
  it, and explicit about unknown outcomes.
- Do not edit generated files under `deploy/build` or vendored/submodule code
  unless the issue explicitly targets them.
- Update resource manifests and registries when adding QML, scripts, images, or
  a self-hosted workload; files on disk alone are not compiled into the app.
- Architectural or cross-repository contract decisions belong in `docs/`; use
  an ADR for decisions that will be expensive to reverse.
- Keep local CMake builds memory-bounded. Use the checked-in presets or `just`
  commands, which default to two parallel jobs. Never invoke bare `--parallel`;
  use `--parallel 2`. Raising the local limit requires explicit user approval.

## Definition of done

A change is complete only when the requested behavior, affected contracts,
documentation, and the strongest available validation are accounted for.
Report successful, failed, blocked, skipped, and not-applicable checks
separately. Always run `git diff --check`; use `validation-and-evidence` to
select build and runtime evidence proportional to the change.

## Validation commands

- Use `just quality check` for the fast repository, embedded-resource, shell,
  and self-hosted registry contract gate.
- Use `just quality test` for the complete deterministic first-party Qt test
  suite.
- Use `just quality client-build` when production C++ or shared build wiring
  changes; it proves only the current host's desktop client target.
- Override the conservative local limit only when explicitly authorized with
  `AMNEZIA_BUILD_JOBS=<count>`.
- `just smoke agent-workloads-remote` is reserved for the explicit disposable
  remote-host scenario. It intentionally fails until GH-1 provides that
  scenario and must never be treated as a successful no-op.
- The disposable DigitalOcean smoke host uses SSH TCP port `48273`, never the
  default port `22`. Restrict its cloud-firewall source to the current
  operator public IPv4 `/32`. Use an SSH key only for host bootstrap; exercise
  the client with the `root` username and password authentication.
