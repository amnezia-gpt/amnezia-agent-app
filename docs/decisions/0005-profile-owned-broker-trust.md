# ADR-0005: Profile-owned broker trust

## Status

Accepted for client implementation (#12, following #10). Workload consumption
and the paired broker E2E remain separate delivery gates (#25 and backend #578).
This replaces the trust-delivery portion of client #9; no separate #9 implementation.

## Context

Recipient enrollment does not install the signing authority required for local
grant verification. The client owns deployment configuration. Local and dev
are independent environments, each with its own complete backend profile.

## Decision

A v3 profile contains `auth_issuer`, `router_base_url`,
`runtime_gateway_base_url`, `ingress_trust_bundle`, `ingress_trust_sha256` and
its environment identifier. The bundle is the exact UTF-8 public export from
`agpt-auth-admin ingress-authority export-public`; the pin is SHA-256 over
those bytes, including whitespace and the final newline. The expected issuer
is the normalized `auth_issuer`, with no additional issuer setting.

The client validates the entire profile before SSH. The bundle must contain
exactly `schema_version=1`, the matching `issuer`, and 1–8 public JWKs. Each
key has exactly `kty=EC`, `crv=P-256`, `use=sig`, `alg=ES256`, unique non-empty
`kid` (at most 128 UTF-8 bytes), and canonical 32-byte base64url `x`/`y` forming
a P-256 point. Unknown/private/symmetric fields, duplicate JSON members,
invalid UTF-8, NUL, an invalid pin or a bundle over 16 KiB are rejected.
Validation never reformats the bundle. Invalid local saves leave the existing
profile untouched. URL and pin input may be trimmed; bundle input is not.

Only Device Gateway receives these environment variables:

| Variable | Value |
| --- | --- |
| `AMGPT_INGRESS_TRUST_BUNDLE` | Exact inline public JSON export, not base64 |
| `AMGPT_INGRESS_TRUST_SHA256` | Lowercase hexadecimal SHA-256 of that export |
| `AMGPT_AUTH_ISSUER` | Profile Auth issuer, also the issuer pin |
| `AMGPT_ROUTER_BASE_URL` | Profile Router URL ending in `/v1` |
| `AMGPT_RUNTIME_GATEWAY_BASE_URL` | Single HTTPS base for check-in and broker dial-out |

No trust input is delivered to OpenClaw/Codex. The bundle is POSIX shell-quoted
as one `--env` argument; placeholders are expanded only in the template, never
inside inserted values. The existing base64 SSH playbook transport preserves
UTF-8, CRLF and final newlines. No command substitution reconstructs the bundle.

Trust participates in the canonical spec hash. Internal reconciliation
resolves the latest complete selected profile and applies it explicitly.
Same-environment signing changes recreate only the gateway with
its existing named state volume; the client does not restart login, delete
recipient identity or remove the sibling runtime. Saved configuration changes
only after successful post-apply observation. Unknown outcomes retain the old
saved profile and can be recovered by repeating explicit apply/inspection.

The current workload UI has no separate backend-profile apply/rotation control
or signing-key explanation. For local testing, save the complete profile in
Dev menu, remove the Gateway service and install it again. Dev profile changes
are delivered in a new client build and used on installation. The internal
reconciliation behavior remains available for future integration with
Amnezia's configuration-delivery flow; this does not implement remote profile
delivery or an automatic rotation workflow.

Inspection and login use the saved profile, independent of global selection.
Removal is independent of profile completeness: it uses an allowlisted
workload/container identity, verifies the observed manager/workload labels,
and confirms container and private-volume absence before clearing the saved service. Legacy
profiles without trust and profiles with invalid pins remain removable.
Unknown outcomes retain the saved service for retry. Explicit removal purges
the selected workload's private named volumes, including logins and device
identity. It also cleans leftover state when the container is already absent.
Shared socket/network resources survive while any container references them;
the selected image reference is removed when unused. The sibling's container
and private state remain. Update/repair and same-issuer trust reconciliation
continue preserving state; uninstall/reinstall requires fresh sign-in and
creates a new device identity. Removal runs asynchronously in the UI with a
bounded remote cleanup and a checked completion marker.
Changing environment identifier or Auth issuer is a conflict, not ordinary
key rotation. Create an independent installation for that environment; this
change does not invent a credential migration/reset operation. Existing v2
configurations can receive trust by explicit apply only when their saved
environment and issuer match the selected catalog.

Observation reads only the five allowlisted public configuration variables
from actual Docker `Config.Env`, not the full environment. Its response is
bounded; duplicates, unexpected fields and oversized values fail closed.
The exact bundle, pin and three URLs must match the desired profile even when
labels match. This proves container configuration, **not** that a workload
process has loaded/accepted trust. That runtime evidence belongs to workload #25.

Dev coordinates and exported trust live in the compiled
`client/server_scripts/amgpt-device-gateway/backend-profile-dev.json` catalog.
There is no fallback to local trust. The dev catalog contains the environment
owner's public export with key `ingress-1db2aedb3e974f7fbe2d23d006f951ed`
and a pin computed over its exact serialization with a final LF. Client
contract tests validate the actual embedded artifact. Local's whole profile
is persisted via Dev menu.
Production remains unconfigured.

For the current development phase, shared dev uses the compiled complete
profile, while local remains operator-configured through Dev menu. Production
profile delivery is deferred to integration with the original Amnezia
repository: extend its existing backend configuration-delivery mechanism to
carry this complete workload profile. Do not introduce a separate remote
profile catalog in this phase. That future integration must retain complete
profile validation before SSH and explicit application to existing deployments;
the gateway continues to receive inline trust rather than fetching keys itself.

## Alternatives considered

Runtime JWKS/key fetching, grant-selected trust, local/dev key substitution,
public device TLS listeners, certificate mounts and a second dial-base were
rejected. Backend `DEVICES__DEVICE_BASE_URL` must match each environment's
public Runtime Gateway base. Profile rotation is an explicit configuration
operation, not background discovery.

## Consequences

Workload #25 must mirror the exact inline environment contract, validate its
bytes and pin independently of TLS configuration, and admit/join sessions
against `AMGPT_RUNTIME_GATEWAY_BASE_URL`. Backend owners export and approve
public signer material and coordinate the public device base. The client does
not create signing authority or publish changes to either companion repo.

Client unit/contract tests and the macOS desktop build do not establish broker
E2E, other platforms, installers or the privileged service. Paired deployment
must additionally confirm loaded trust, enrollment, check-in, `joined`, and a
real Codex answer after workload #25 is delivered.
