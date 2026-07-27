# The region-session transport stack: options

Decision support for [CLIENT2.md](CLIENT2.md) build-order step 5, "The region
session" — the C++ QUIC/WebTransport stack that CLIENT2.md deliberately left
open, and the WebSocket fallback leg beside it. This document lays out the
field and makes a recommendation; it decides nothing. Library facts were
verified against upstream repositories and vcpkg in July 2026.

## What is being chosen

The **region server's** transport stack: TLS listener, HTTP/3 + WebTransport
for the primary leg, TLS + WebSocket for the fallback leg, and the outbound
call-home leg of the relay design (CLIENT2.md, "Reaching home-hosted
regions"). The **native client's** half is chosen in the client repository
(its ADR 0004 leaves it open), but reuse of one stack on both ends is worth
points here because the version-floor rule below binds them together.

Not being chosen: anything for the browser build — WebTransport and WebSocket
are browser platform APIs, and no library here can even run under WASM (no
raw UDP). Browser support is no longer a constraint: WebTransport is Baseline
across Chrome 97+, Edge 98+, Firefox 114+, and Safari 26.4+.

## Requirements, from the standing decisions

1. **QUIC means TLS 1.3, and WebTransport means HTTP/3.** A stack that stops
   at QUIC (msquic) leaves the H3 mapping and the WebTransport session layer
   — capsules, extended CONNECT, `SETTINGS_WEBTRANSPORT_MAX_SESSIONS`, draft
   negotiation — to us.
2. **The fallback leg needs TLS too.** A browser will not open a plaintext
   WebSocket from an HTTPS page. A **relayed** region, though, terminates
   nothing: TLS ends at the grid's edge and the region holds an ordinary
   outbound client-TLS connection. Only **directly serving** regions need a
   TLS listener at all.
3. **The version-floor rule applies in full** (CLIENT2.md, "Library
   choices"): this dependency sits on both sides of the wire and on machines
   the operator does not own, so it must be pinnable to a
   lowest-long-term-supported version. This matters more than usual here —
   see the standardization note.
4. **Build reality:** the region builds with MSVC + vcpkg (manifest) on the
   Windows dev box and natively with classic vcpkg on the Linux cloud box.
   A library outside vcpkg costs an overlay port or vendoring; a library
   whose vcpkg port excludes Windows costs the dev loop.
5. **The region has zero networking dependencies today** and hand-rolls
   HTTP/1.1 over raw sockets. Whatever is adopted is the region's first
   networking and first TLS dependency; weight counts double.

## The standardization problem, stated plainly

WebTransport is **not an RFC**. The wire protocol is
`draft-ietf-webtrans-http3-16` (July 2026), and browsers negotiate among
draft versions via per-draft settings codepoints — a server today must speak
draft-02 plus settings-based negotiation for draft-07+ to interoperate with
current Chrome and Firefox. Server libraries lagging the browsers' draft is
the number-one practical interop hazard, and it cuts against the version-floor
rule directly: **a moving draft cannot be a floor.** Whatever QUIC stack is
chosen, its WebTransport layer will churn until the RFC lands; WebSocket
(RFC 6455) has been frozen for a decade.

## The QUIC field

| Library | WebTransport server | Windows + vcpkg | TLS backend | Weight | Note |
| --- | --- | --- | --- | --- | --- |
| **picoquic** | Native, client + server, tracks drafts | Windows CI, **not in vcpkg** | picotls (+OpenSSL/MbedTLS crypto) | Light | MIT; one primary maintainer (an IETF QUIC co-author); research-grade but very active |
| **msquic** | None — QUIC only; H3 (`msh3`) separate and young | vcpkg `msquic` 2.4.8, first-class Windows | SChannel or quictls | Light | MIT; the .NET/Windows production QUIC; WT layer would be ours or the young `libwtf` |
| **lsquic** | Native server since 4.3.0; draft version undocumented, client unclear | **Not in vcpkg** | BoringSSL (pinning pain) | Moderate | MIT; LiteSpeed production; verify Chrome interop before trusting |
| **ngtcp2 + nghttp3** | Primitives only — most DIY of all | vcpkg 1.24.0/1.17.0 | Widest choice (OpenSSL 3.5+, wolfSSL, GnuTLS…) | Light | MIT; curl's H3 stack; excellent QUIC, no WT session layer |
| **Google quiche** | Best-in-class — Chromium's own | **Not in vcpkg**, Bazel-first, BYO build files | BoringSSL | Heavy (Abseil, Envoy-style build tax) | BSD-3; the correctness ceiling, at the highest integration price |
| **proxygen + mvfst** | Native, Meta-proven | vcpkg exists **but excludes Windows** | fizz | Heaviest (folly, wangle, boost×8) | Hard stop for the MSVC dev loop |
| **Cloudflare quiche** | Primitives only, via C FFI | Not in vcpkg; Rust toolchain in the build | BoringSSL | Moderate-heavy | Already set aside by CLIENT2.md; nothing above reverses that |

Two corrections this research makes to CLIENT2.md's provisional slate: the
msquic-versus-ngtcp2 framing understated that **neither ships WebTransport**
— both leave the layer browsers actually speak to us — and the libraries that
do ship it natively (picoquic, lsquic, Google quiche, proxygen) each fail one
of our build constraints (vcpkg, Windows, or weight).

## The WebSocket leg

Needed regardless of the QUIC choice, for the fallback and plausibly for the
call-home relay leg. The field, all in vcpkg:

- **libwebsockets** (C, MIT-ish/LGPL-SA exception, vcpkg 4.5.8) — TLS +
  HTTP/1.1 + WebSocket, **server and client roles**, tiny footprint, event
  loop of its own or pluggable. The one candidate that covers the listener
  *and* the outbound call-home connection with a single light dependency.
- **Boost.Beast** (vcpkg boost 1.91) — header-only over Asio + OpenSSL; more
  assembly required (HTTP server loop is ours), and it pulls Boost into a
  codebase that does not use it.
- **uWebSockets** (vcpkg 20.78) — fast, server-oriented; no client role, so
  the relay leg would need something else anyway.

## Options

**A. WebSocket first — recommended.** Ship the region session over TLS +
WebSocket (`transports: ["websocket"]`), using **libwebsockets** for the
region's listener and its call-home client leg. Add WebTransport as a second
advertised transport later, when two things exist that do not now: Phase 2
rate measurements (the numbers CLIENT2.md says should size this decision) and
a WebTransport RFC (a floor the version rule can actually hold). This is not
a compromise path in the probe's design — `transports` is data, the client
adapts, and every browser that speaks WebTransport speaks WebSocket. It is
the shortest path to scene traffic — chat included — reaching the client,
and it front-loads the work every option shares (TLS, the accept-loop
restructure, ticket validation, the shared protocol library) while deferring
only the layer that is still churning.

**B. WebTransport now, via picoquic (vendored).** The only light stack with
real WebTransport in both roles. Costs: an overlay port or vendored CMake
build (not in vcpkg), a bus factor of roughly one, and living on the draft
treadmill until the RFC. Choose this if QUIC-from-day-one outweighs the
sequencing argument.

**C. WebTransport now, via msquic + our own (or libwtf's) WT layer.** The
best Windows/vcpkg citizenship in the field, and the WT layer stays under our
control — but writing extended CONNECT, capsules, and draft negotiation
ourselves is precisely the hand-rolling-hostile-parsers move the grid channel
decision warned against, and `libwtf` is too young to carry it for us yet.

**D. Google quiche.** The correctness ceiling; adopt only if we accept
Envoy-grade build integration as a permanent tax. Not proportionate to a
region server today.

Under every option, the **shared protocol library** (CLIENT2.md, "A shared
C++ protocol library") is unchanged: it sits above the byte stream, carries
no transport, and starts with option A's stream exactly as well as with QUIC
streams later.

## Recommendation

**A now; revisit B-versus-C when the WebTransport RFC publishes or Phase 2
produces rate numbers, whichever comes first.** The region gains TLS, a real
listener, ticket validation, and the session protocol — all of which every
option needs — on a frozen spec with a light vcpkg dependency, and `llSay`
reaches the first-party client months before the QUIC question needs an
answer. The deliberately unfashionable observation: nothing in the region
session's Phase 1 traffic is known to need QUIC yet, and the document that
would prove it needs QUIC is the one Phase 2 exists to write.
