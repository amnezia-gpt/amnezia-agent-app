# ADR-0006: Workload-announced browser links

## Status
Accepted for the client implementation. Updated for agent-workloads #31
on 2026-10-10; the earlier state-bearing development contract is replaced.

## Context

Amnezia manages installed self-hosted applications. Backend profiles configure
the workload's issuer, endpoints and trust. A WebUI origin or route template
in those profiles would make the client depend on backend application routing.
Device Gateway exposes links-v1 through `workloadctl links --json`, relaying a
complete link announced by backend check-in. The contract is defined in
agent-workloads #31 and
`workloads/amgpt-device-gateway/contracts/links-v1.schema.json`.

The earlier `state` field duplicated login/connectivity status and could report
`login_required` after login had completed, before the next check-in. Reasons
for missing links do not belong in this browser-navigation contract.

## Decision

Read links through the existing authenticated SSH path to the installed
Gateway's private management socket. Do not introduce a public listener,
a client backend request, a WebUI profile field or route construction from
device identity. A link describes a browser entry point; it does not prove
that the destination is healthy or grant browser authorization.

A successful command (exit 0) returns exactly `{schema_version:1,links:[...]}`.
There is zero or one selected `{rel,type,href}` record. Only
`urn:amnezia-gpt:link-relation:client-launch:v1` with `text/html` has browser
semantics. Unknown relations/media types are ignored. Gateway selects the
first supported link from the backend list of up to four entries. An empty
list is normal absence: hide **Open WebUI**, without inventing a reason.

Exit 1 returns `{schema_version:1,error_code:"gateway-unavailable"}` without
`links`. Treat it as command failure; even with exit 0 it must not become empty
success. Exit 2 is invalid invocation/unsupported image and hides the action.
Reject duplicate keys, unexpected fields (including the removed `state` and
legacy `kind`), invalid UTF-8/schema, oversized documents and unsafe URLs.
Command/transport errors remain separate from successful empty results.

Read once after management-page state retrieval and **Refresh state**. Keep
only availability in the page. On activation read again, validate the complete
URL and open it with the system browser. A fresh empty/error response hides
the action and does not open the browser. Do not cache, persist or log href,
schedule polling/retries, or read login/connectivity status to explain absence.
Sign-in status retrieval does not trigger another links request.

The SSH transport does not expose remote process exit status. Encode the JSON
and explicit exit marker as bounded base64 because the transport decodes each
read chunk separately to QString. Decode once to preserve UTF-8, require the
exit marker, and bound the decoded JSON to 8192 bytes. Successful transport
alone cannot advertise a link.

Accept HTTPS, or HTTP restricted to loopback; href is at most 2048 UTF-8 bytes,
without userinfo, whitespace, backslash or control characters. Query/fragment
remain opaque, with no issuer/resource interpretation or route reconstruction.
The browser boundary uses Qt 6.10 `QUrl::StrictMode`; see the
[QUrl documentation](https://doc.qt.io/qt-6.10/qurl.html).

## Alternatives considered

- Put WebUI routes into local/dev profiles: rejected because the client would
  own backend application routing.
- Explain absence with another links-state machine or automatic login and
  connectivity reads: rejected as duplicate semantics and extra SSH work.
- Keep the action always visible: rejected in favor of the agreed link-present
  behavior.
- Cache href or navigate on discovery: rejected because links can change after
  logout/check-in and navigation requires user activation.

## Consequences

Local/dev/prod routing belongs to backend/WebUI configuration. The native
client remains an application manager; it needs no device-ID route template.
Link updates become visible on the next explicit page/state refresh; signing
in and receiving a link are independent operations. The extra read on click
prevents opening a cached URL after withdrawal. WebUI authorization governs
the remaining race between reading and browser navigation.

This is a coordinated incompatible change to development-only v1: update the
client and Gateway together. No dual-format fallback or v2 is introduced.
Gateway must understand backend links before router #604 announces them.
Workload owns link clearing on logout/lost authorization, ignoring in-flight
check-ins after logout, and preservation during temporary failures.

## Implementation validation plan

Focused RED/GREEN tests cover successful empty/current documents, launch URLs,
error documents with correct and wrong exit status, malformed/legacy records,
and the existing URL/byte/JSON restrictions. SSH/controller tests exercise
normal absence separately from process failure and preserve fresh reads.
Production QML tests prove conditional visibility, withdrawal on click, and
absence of links reasons/extra sign-in-triggered requests. Run the repository
contract gate, complete first-party suite, macOS desktop build with two jobs,
`git diff --check`, and inspect rendered production-page evidence.
Local checks do not establish live backend/browser E2E or other platforms.


Validation completed on 2026-10-10 against agent-workloads `933dce4` (#31):
RED reproduced rejection of empty and nonempty successful documents without
`state`. The same focused command passed after the parser update; the complete
parser suite passed 77 cases. Error-document tests characterize failure with
both correct and incorrect process status. Production QML checks passed for
visibility, link withdrawal on activation, removal of absence explanations,
and no extra links read after sign-in status. Rendered absence-page evidence
was visually inspected. With the `existing-debug` preset,
`just quality check` passed 5/5, `just quality test` passed 11/11, and
`just quality client-build` built the macOS desktop target with two jobs.
`git diff --check` passed. Existing Qt minimum-deployment-version linker and
unrelated nodiscard warnings remain. No new important review findings were
identified in the changed paths. Live backend/browser navigation, remote image
rollout, privileged-service packaging and other platforms were not tested.
No issue edits, commits, merges or publication were performed by this task.
