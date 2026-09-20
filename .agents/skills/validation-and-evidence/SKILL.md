---
name: validation-and-evidence
description: Selects and reports bounded validation for this multi-platform Qt/CMake client, privileged service, embedded SSH scripts, and remote workloads. Use after changes, before merge, when defining acceptance criteria, or when deciding what local and CI evidence actually proves.
---

# Validation and Evidence

## Discover before running

Inspect the changed files, `CMakeLists.txt`, `deploy/build.sh`, relevant GitHub
Actions jobs, resource manifests, submodules, and available local SDKs. Do not
invent a test command or claim that vendored tests cover the product.

## Evidence ladder

Choose the strongest applicable and available evidence:

1. **Repository hygiene**
   - `git diff --check`
   - inspect `git status --short` and the complete diff
   - validate changed repo-local skills with the Agent Skills validator
2. **Static consistency**
   - verify new files are registered in CMake/QRC and referenced paths exist
   - run `.clang-format` only on changed C/C++ lines/files when formatting is in scope
3. **Configure/build**
   - initialize submodules for a clean checkout
   - macOS client-only debug target when appropriate:
     `cmake -S . -B deploy/build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.1/macos"`
     followed by
     `cmake --build deploy/build --target AmneziaVPN --parallel 2`
   - local build commands must always specify at most two parallel jobs unless
     the user explicitly approves a higher limit; never use bare `--parallel`
   - use `deploy/build.sh` or the corresponding CI job for a full host build,
     service, installer, cross-compile, or packaging claim
4. **Behavioral evidence**
   - launch the built app for UI/client behavior
   - use a disposable remote Linux host for SSH deployment and container lifecycle
   - verify workload internals in `agent-workloads`
5. **Platform/CI evidence**
   - rely on the matching GitHub Actions job for platforms unavailable locally;
     one platform never stands in for another

## Reporting

For each claim record the exact commit/worktree, environment, command or manual
scenario, result, and limitation. Classify required checks as `satisfied`,
`not_satisfied`, `inconclusive`, `blocked`, or `not_applicable`.

Never say "validated" or "all checks passed" without naming the boundary. A
successful macOS `AmneziaVPN` target does not prove the privileged service,
Network Extension, signing, installer, remote Docker lifecycle, or other OSes.
