---
name: source-driven-development
description: Grounds Qt, CMake, Conan, Apple, Android, SSH, Docker, and OAuth implementation decisions in the versions and authoritative sources used by this repository. Use when changing framework-specific behavior, build configuration, platform APIs, protocols, dependencies, or security-sensitive integration code.
---

# Source-Driven Development

## Source order

1. Existing repository behavior and pinned configuration.
2. Exact dependency source or submodule revision used by this checkout.
3. Official version-matched documentation or standards.
4. Upstream Amnezia implementation and history.

Do not use tutorials, memory, or an unrelated latest-version example as the
primary basis for a framework-specific change.

## Workflow

1. Detect versions from `CMakeLists.txt`, `conanfile.py`, GitHub Actions,
   submodule gitlinks, and the configured Qt installation. Current headline
   constraints include C++17, CMake 3.25+, Qt 6.10+, and Conan-driven packages.
2. Inspect how the same concern is implemented elsewhere in the repository and
   across platform variants before introducing a new pattern.
3. Read the narrow authoritative page or source corresponding to the detected
   version. For OAuth/OIDC, prefer the governing RFCs and provider documentation.
4. Implement consistently with both the authoritative contract and established
   project architecture. Surface conflicts rather than silently choosing one.
5. Record non-obvious sources in the design document, ADR, or change report.
   Source comments belong in code only when they explain a durable constraint.
6. Mark behavior unverified when no authoritative source or executable evidence
   can be obtained.

## Guardrails

- Do not upgrade dependencies incidentally.
- Verify API availability on every claimed target platform.
- Treat upstream code as implementation evidence, not proof that it is safe for
  our new workload or authorization boundary.
