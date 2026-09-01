---
name: code-review-and-quality
description: Reviews changes to the Amnezia agent app across C++/Qt, QML, CMake/Conan, shell, Docker, IPC, SSH deployment, and platform-specific code. Use after implementation and before merging any feature branch into amnezia.
---

# Code Review and Quality

## Review order

1. Read the issue/spec and identify the intended behavior and excluded scope.
2. Inspect the complete diff, including generated-looking resources, scripts,
   translations, submodule gitlinks, and cross-repository contract changes.
3. Review available tests and validation evidence before trusting the implementation.
4. Trace every changed path through success, error, cancellation, cleanup,
   update, restart, and removal where applicable.

## Axes

- **Correctness:** state transitions, ownership, null/empty input, persistence,
  error mapping, partial SSH/container outcomes, and platform guards.
- **Architecture:** preserve UI -> controller -> core -> SSH/service boundaries;
  avoid business logic in QML and avoid parallel deployment abstractions.
- **Registration:** enums, serialization, factories, `ContainerUtils`, CMake,
  QRC manifests, translations, packaging, and platform-specific variants agree.
- **Security:** validate remote and UI input; prevent shell injection, path
  traversal, unsafe permissions, credential persistence, and sensitive logging.
- **Concurrency/resources:** QObject lifetime, signal connections, thread
  affinity, cancellation, sockets/processes, callbacks, and remote side effects.
- **Compatibility:** persisted configuration, older remote installations,
  update/removal behavior, architectures, OS gates, and upstream mergeability.
- **Simplicity:** follow existing patterns, avoid speculative abstractions and
  unrelated refactors, and prefer existing dependencies.
- **Evidence:** ensure claims match the actually built target and exercised boundary.

## Findings

Report findings first, ordered by severity, with exact files and tight line
ranges. Use `Critical`, `Important`, or `Suggestion`; explain the failing
scenario and required correction. If no finding exists, say so and list residual
validation gaps. Never substitute a summary for review evidence.

## Merge standard

The change may merge into `amnezia` only when critical/important findings are
resolved or explicitly accepted by the responsible human, relevant docs and
contracts agree, and required validation is satisfied or transparently blocked.
