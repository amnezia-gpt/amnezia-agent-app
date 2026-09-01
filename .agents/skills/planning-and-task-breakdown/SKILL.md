---
name: planning-and-task-breakdown
description: Produces implementation-ready plans for changes to the Amnezia Qt client, service, SSH deployment pipeline, embedded container scripts, and companion workload repositories. Use for multi-layer, cross-platform, cross-repository, security-sensitive, or externally observable work.
---

# Planning and Task Breakdown

## Workflow

1. Read the issue, relevant docs, current implementation, CMake/resource
   registration, and adjacent platform variants before proposing tasks.
2. Trace the current path end to end. For self-hosted deployment this normally
   means QML/UI -> UI controller -> core install controller -> container enum
   and metadata -> script registry/resource bundle -> SSH upload/execution ->
   remote container -> returned configuration/status.
3. Define the target behavior and stable contracts before file-level tasks:
   inputs, outputs, states, errors, ownership, timeout/cancellation behavior,
   sensitive data, compatibility, and rollback/removal.
4. Split work into reviewable vertical increments. Keep workload implementation
   in `agent-workloads`; keep client enumeration, UI, SSH orchestration, and
   embedded deployment assets in this repository.
5. For every task name:
   - exact files or discovery anchors;
   - behavior and invariants;
   - dependencies and ordering;
   - validation method and required environment;
   - completion evidence.
6. Include documentation and resource/registry updates in the task that changes
   the contract, not as unspecified cleanup.
7. Mark unavailable environments, Apple signing, remote hosts, and other
   external prerequisites as explicit validation gaps.

## Plan quality

- Do not use phase names such as "backend" or "testing" without concrete work.
- Do not invent files or APIs; cite inspected paths and symbols.
- Separate refactoring from behavioral changes unless inseparable.
- Keep the repository buildable at each mergeable increment.
- Plans are not completion; continue implementation when authority is already clear.

## Output

Lead with the intended behavior, then list ordered tasks, dependencies,
acceptance criteria, validation evidence, rollout/rollback considerations, and
open decisions.
