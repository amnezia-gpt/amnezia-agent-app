set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

agent-workloads-remote:
    #!/usr/bin/env bash
    set -euo pipefail
    echo "The GH-1 agent-workloads remote scenario is not implemented yet." >&2
    echo "First manual dev run: docs/sdlc/agent-workloads-dev-smoke.md" >&2
    echo "This placeholder intentionally fails; it must never report a no-op success." >&2
    exit 2
