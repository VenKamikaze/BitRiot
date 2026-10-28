# 11 — vnet WebTransport datagrams

- **Repo:** `VenKamikaze/vnet`
- **Branch:** per the vnet repo's convention
- **Depends on:** 10
- **Status:** todo

## Goal

Give browser clients an **unreliable datagram** path to the relay using WebTransport, the closest thing browsers have to UDP. If WebTransport is unavailable or fails, fall back automatically to the WebSocket path from brief 10.

Lockstep Bit Riot works either way. This transport is mainly for client/server games such as Odamex, where retransmit stalls hurt, and for smoother lockstep play under packet loss.

## Background

These are claims from the original design discussion. **Re-verify them, since browser support moves fast:**

- WebTransport reached Baseline in March 2026, when Safari 26.4 shipped it. Chrome and Edge have had it since 2022, Firefox since 2023.
- Safari support depends on the **OS version** (iOS/macOS 26.4), not the Safari version.
- Engines differ on some server and certificate details. Keep the WebSocket fallback.
- WebTransport needs an HTTP/3 server with TLS. `quic-go/webtransport-go` is the mature Go choice.
- For local development without a public certificate, browsers accept `serverCertificateHashes`. These require a short-lived certificate: ECDSA, valid for ≤14 days. Confirm the current rules.

## In scope

1. **Relay**
   - Add a WebTransport endpoint (HTTP/3, UDP 443 or a configurable port) using `webtransport-go`.
   - Carry the brief 10 framing:
     - control messages over a reliable bidirectional stream;
     - `DATA` over WebTransport datagrams.
   - Members on any transport (WS, WT, UDP) share rooms transparently.
2. **Certificates**
   - **Production:** a real certificate. This is a deployment note; ACME through the relay or a provided cert/key.
   - **Development:** a `--dev-cert` mode that generates a short-lived ECDSA certificate and prints its SHA-256 hash for `serverCertificateHashes`.
3. **Browser backend**
   - Add `VNET_BACKEND_RELAY_AUTO`: try WebTransport, and fall back to WebSocket after a timeout or error.
   - Report which transport was chosen via `vnet_status()` or `vnet_transport_name()`.
   - Keep the same C ring-buffer approach as 10.
   - Datagram size: `VNET_MAX_DATAGRAM` (1100) plus vnet framing must fit the browser's datagram size limit. Check `datagrams.maxDatagramSize` at runtime and fail clearly if it's smaller.
4. **Docs:** a transport comparison table in vnet's README: semantics, browser support, hosting requirements.

## Out of scope

- A native WebTransport client. Native stays on `RELAY_UDP`.
- Congestion-control tuning.

## Tests

- **Go tests:** a WebTransport session joins a room, and exchanges datagrams with a UDP member and with a WS member.
- **Playwright Chromium:** the test page connects to a local relay in dev-cert mode, using `serverCertificateHashes` passed via the page URL. It shows `transport == webtransport`, exchanges datagrams with a native client, and under a deliberately broken WT endpoint falls back to WS. Check Chromium's flags for local QUIC if needed.
- **Loss behaviour (report only):**
  - With impairment at the relay at 10% loss, the WT path shows lost datagrams arriving without stalls.
  - The WebSocket path can't be tested the same way. Application-level drops at the relay show up as loss, not as TCP head-of-line stalls. Real stalls need kernel-level loss (`tc netem`, which needs NET_ADMIN and is probably unavailable).
  - Measure the WS stall behaviour only if `tc netem` works. Otherwise say so, and record whatever numbers you have in the Outcome.
- **Datagram size:** check `datagrams.maxDatagramSize` in Chromium against `VNET_MAX_DATAGRAM` (1100) plus framing.

## Acceptance criteria

- Browsers that support WebTransport use it.
- Others fall back to WebSocket without code changes in the game.
- Dev-cert mode works for local testing.
- Production deployment is documented.

## Outcome

*(fill in when done)*
