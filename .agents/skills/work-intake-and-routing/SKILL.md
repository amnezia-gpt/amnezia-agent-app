---
name: work-intake-and-routing
description: Classifies incoming work for the Amnezia agent-app fork and selects the smallest safe delivery route. Use when accepting or triaging an issue, feature, bug, investigation, upstream change, workload addition, OAuth change, or platform-specific request before implementation.
---

# Work Intake and Routing

## Workflow

1. Inspect `git status --short --branch`, remotes, submodules, related docs,
   affected code, build files, and existing issue context without changing state.
2. Separate the desired outcome from the proposed implementation.
3. Identify affected surfaces: UI/QML, client core, IPC, privileged service,
   SSH deployment, embedded scripts, remote workload, packaging, signing, and
   platform-specific code.
4. Identify cross-repository contracts, especially with `agent-workloads`, and
   name which repository owns each side.
5. Select the smallest adequate route:
   - `direct-change` for narrow docs or mechanically safe changes;
   - `investigation` when existing behavior or ownership is unclear;
   - `planned-change` for multi-file implementation with known design;
   - `specification` for a new protocol, workload lifecycle, security boundary,
     or cross-repository interface;
   - `upstream-sync` only for moving `upstream/dev` into `main` and then
     reconciling `main` into `amnezia`;
   - `hold` when authority, credentials, signing, target infrastructure, or a
     product decision is genuinely required.
6. State acceptance criteria, exclusions, risks, validation boundaries, and the
   immediate next action. Escalate the route if later evidence widens the scope.

## Guardrails

- An issue authorizes work only inside the issue boundary.
- A local client build does not validate the service, remote host, or another OS.
- Do not hide a new protocol or security decision inside a direct code change.
- Ask only for decisions that cannot be discovered or safely deferred.

## Report

State the need, affected baseline, selected route, rationale, acceptance
criteria, repositories and surfaces involved, validation plan, assumptions,
and blockers.
