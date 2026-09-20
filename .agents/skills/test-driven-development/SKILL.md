---
name: test-driven-development
description: Drives observable behavior changes and bug fixes in the Amnezia C++/Qt client through focused RED-GREEN-REFACTOR evidence. Use when adding logic, changing runtime behavior, or fixing a defect in C++, Qt/QML controllers, embedded SSH scripts, or self-hosted workload integration. Do not force TDD onto documentation-only, translation-only, static resource registration, or mechanical build-metadata changes.
---

# Test-Driven Development

Use the smallest executable test that proves the requested behavior. Keep the
cycle visible and report the evidence; do not merely add tests after a finished
implementation and label the work TDD.

## Choose the test boundary

- Prefer a focused QtTest against production translation units for C++ logic.
- Test controller/model state, signals, serialization, validation, and public
  outcomes instead of private methods or exact call sequences.
- Use `QSignalSpy` when the observable contract is a Qt signal.
- Use data-driven QtTest rows for input matrices and boundary cases.
- Put fakes at actual process boundaries: SSH, Docker, browser launch, network,
  filesystem, clocks, or randomness. Prefer real project objects inside them.
- For embedded shell and Docker integration, use deterministic contract tests
  for resource registration, generated commands, substitutions, validation,
  and secret handling. Reserve real remote execution for the explicit
  disposable-host smoke scenario.
- For QML, prove controller/model behavior below the view first. Use runtime or
  manual UI evidence only for navigation, binding, layout, and visual behavior
  that cannot be established below QML.

## Run RED-GREEN-REFACTOR

### 1. RED

1. Add one narrow test for the missing behavior or defect.
2. Run that test alone before changing production behavior.
3. Confirm that it fails for the intended reason. Record the command and the
   meaningful failure.
4. If it passes, the test does not prove the requested change. Improve the test
   or establish that the behavior already exists before proceeding.

For a bug, the failing test is the reproduction. It must fail on the defective
path and pass once the defect is fixed.

### 2. GREEN

Implement only enough production code to satisfy the failing contract. Re-run
the exact RED command and confirm it passes.

### 3. REFACTOR

Remove duplication and improve names or boundaries without changing behavior.
Re-run the focused test after every material refactor.

### 4. Regression gates

After the focused cycle:

1. Run `just quality check`.
2. Run `just quality test` for behavioral code changes.
3. Run `just quality client-build` when production C++ or shared build wiring
   changed.
4. Run `just smoke agent-workloads-remote` only when the task explicitly
   authorizes a disposable remote host and the scenario exists.
5. Always run `git diff --check`.

## Legacy and already-written code

- Before refactoring untested legacy code, add characterization tests for the
  behavior that must remain stable. Then make the desired behavior change with
  a new RED test.
- If production code was already written before this skill was invoked, do not
  revert or sabotage it solely to manufacture a RED result. Add the strongest
  regression or characterization test available and report the sequence as
  test-after, not TDD.
- Pure documentation, translations, QRC registration, and mechanical CMake
  metadata may have no meaningful RED test. Validate them with the narrowest
  applicable repository contract gate and state why TDD was not applicable.

## Test quality rules

- Name tests after observable behavior and conditions, not implementation
  methods.
- Keep Arrange, Act, and Assert easy to distinguish.
- Assert one behavioral concept per test; multiple assertions are fine when
  they describe the same outcome.
- Avoid sleeps, live network access, shared mutable state, execution-order
  dependencies, and reliance on the developer machine.
- Exercise malformed, missing, oversized, unknown-enum, schema-version, and
  trailing-output cases where inputs cross a trust boundary.
- Never place passwords, keys, authorization codes, tokens, cookies, complete
  callback URLs, or credential-bearing configuration in fixtures, logs, or
  failure messages.
- Link the real production source into tests. Do not duplicate production
  algorithms inside test helpers.

## Report evidence

For each behavioral slice, report:

- RED: focused command, failed test, and why the failure proved the gap;
- GREEN: the same command and its passing result;
- REFACTOR: whether cleanup occurred and the focused re-run result;
- regression: broader gates that passed;
- blocked, skipped, or not-applicable checks with the concrete reason.

Never claim platform, privileged-service, or remote-container coverage from a
local desktop unit test or build.
