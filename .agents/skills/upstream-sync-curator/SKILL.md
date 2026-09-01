---
name: upstream-sync-curator
description: Synchronizes the Amnezia fork without mixing upstream history with product development. Use when fetching upstream, updating main from upstream/dev, merging main into amnezia, inspecting divergence, or resolving synchronization conflicts.
---

# Upstream Sync Curator

## Branch contract

- `upstream/dev` is the source baseline.
- `main` is synchronization-only and is published as `origin/main`.
- `amnezia` carries our integrated work and receives `main` after synchronization.
- Feature branches start from and return to `amnezia`.

## Workflow

1. Require a clean worktree and inspect `git remote -v`, `git branch -vv`, and
   the ahead/behind relation among `upstream/dev`, `main`, and `amnezia`.
2. Fetch both remotes. Never push to `upstream`.
3. Update local `main` from `upstream/dev`. Prefer fast-forward when histories
   allow it; stop and explain unexpected local-only commits on `main`.
4. Verify the exact upstream commit and review release/build-system changes that
   may affect our fork.
5. Publish `main` to `origin/main` only when explicitly authorized by the task.
6. Merge `main` into `amnezia`; do not rebase the shared integration branch.
7. Resolve conflicts by preserving upstream behavior and reapplying our intent.
   Re-run validation for every affected platform or boundary.
8. Publish `amnezia` only when explicitly authorized and report both resulting
   commit identities.

## Guardrails

- Never merge feature work into `main`.
- Never force-push shared branches unless the user explicitly requests it and
  the exact consequences are understood.
- Do not treat a clean textual merge as behavioral compatibility.
- Preserve submodule gitlinks exactly unless upstream or the issue changes them.
