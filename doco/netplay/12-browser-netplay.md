# 12 — Browser netplay

- **Repo:** BitRiot (the vnet changes needed go to the vnet repo)
- **Branch:** `netplay/12-browser`
- **Depends on:** 07, 08 (screens and settings file), 09 and 10. Brief 11 has **landed**, so the browser uses `RELAY_AUTO`. Brief 14 (vnet 0.4.0, tag `v0.4.0`) also landed: `RELAY_AUTO` now works natively too (one `vnet_config` with `relay` and `relay_udp` for both worlds), `vnet_status_info` has `relay_rtt_ms` and `max_members`, and the relay client survives a hidden tab or a sleeping laptop. Read vnet's `doco/integration.md` and brief 14's follow-ups for 12. Both the BitRiot and vnet repos must be attached.
- **Status:** todo

## Goal

Browser players and native players join the same Bit Riot match through the vnet relay using a room code. The lockstep netplay from brief 07 runs unchanged over the relay backends. Brief 05b has already proved that the sim alone is bit-identical in wasm. This brief confirms that the full game build is too.

## Read first

- The Outcome sections of briefs 07 and 09, and of vnet briefs 10 and 11 (in vnet's `doco/`).
  - They record what the relay actually does: room creator is `10.0.0.1`; `vnet_status()` reports CONNECTING/READY/FAILED and the transport (`relay-wt` or `relay-ws`); `room-host:PORT` resolves to the host.
  - Operational limits to design around: a silent UDP member is dropped after ~15 s (`-session-timeout`); per-address caps (128 members, 128 WebSockets, 32 pending WebSockets, 128 WebTransport connections) matter behind one NAT; a global `-max-wt` (default 4000) caps WebTransport connections; WebTransport needs `https://host/vnet`, WebSocket `wss://host/vnet`; a dev certificate's hash changes on every relay restart.
  - `relay/README.md` in vnet covers deployment (TLS, Docker, sizing).
- [`README.md`](README.md): D6 and the network notes.
- Brief 05b's Outcome, for the gate results and the emsdk setup.

## In scope

1. **Bit Riot on the relay**
   - Wire the netplay layer to vnet's relay backends:
     - the browser uses `RELAY_AUTO` (WebTransport, falling back to WebSocket);
     - native uses `RELAY_UDP`.
   - The host creates a room; joiners enter the room code.
   - By relay convention (10), the host is `10.0.0.1`.
   - Direct native `--host`/`--join` over plain UDP stays available.
2. **UI**
   - Add "Host via relay" (it shows the room code big and copyable) and "Join by room code" to brief 08's screens.
   - Relay URL: a sensible default from build config, overridable in settings.
3. **Settings persistence on web:** the name, last room and relay URL use IDBFS or localStorage. Brief 08 introduced a settings file; pick the simplest option that works with it.
4. **Full-game determinism check.** Brief 05b already proved the sim under Node. Here, confirm that the *full game's* wasm build agrees: replay the golden record through the game with a Playwright test hook, and check it matches native checkpoints. Add this to the test scripts. If it fails, stop and report the first diverging tick and a dump diff.
5. **Background tabs.**
   - Browsers stop `requestAnimationFrame` in hidden tabs. Timers are throttled to roughly 1 Hz, and more heavily after about 5 minutes. A backgrounded peer would therefore stall everyone, and "step from a timer" does **not** keep up.
   - Options that do work:
     - **(a)** Drive stepping from incoming network messages. Message events aren't throttled. vnet 0.4.0's `vnet_config.wake` callback is the hook for this (see `wake_probe.c` in vnet), but it does not replace a timer-driven poll: vnet's own retries, fallback deadline and keepalives only run inside `vnet_poll()`, so keep a timer poll (about 1 Hz is enough) alongside it.
     - **(b)** Tick from a dedicated Worker timer that posts to the main thread.
     - **(c)** A tab that is playing audio is exempt from throttling.
   - Pick one, measure it in Chromium, and document it.
   - If hidden-tab stepping still can't keep up, notify the host and let that slot fall back to AI after a grace period, using brief 07's leave path. Brief 13 adds reconnection.
   - **A hidden host** stalls everyone, because there is no host migration. Show peers a clear "host is away" state.
   - Playwright can't truly hide a tab. Test with CDP `Page.setWebLifecycleState({state: 'frozen'})` and/or by overriding `document.visibilityState` and dispatching `visibilitychange`, and say which you used.
6. **Deployment note**
   - How to serve the wasm build and point it at a relay. The page must be HTTPS to use `wss://` and WebTransport.
   - Hosting choices, such as GitHub Pages for the static files plus a small VPS for the relay, are for the user to decide. Write the steps; don't deploy anything.

## Out of scope

- Hot-join (13).
- Mobile.
- Public deployment.

## Tests

- **Playwright, two browser contexts plus one native `bitriot_headless`:**
  - a local relay runs;
  - the native peer hosts a room, and two browser peers join by code;
  - they play an AI-heavy match to the end with some scripted inputs;
  - all peers report identical checkpoint hashes and the same winner.
- **The same with impairment** at the relay. If brief 11 is in, run it on both WT and WS.
- **The full-game golden-record replay** matches native (item 4).
- **Background-tab behaviour:** emulate a hidden page as described in item 5 and check the documented outcome.

## Acceptance criteria

- A native and a browser player complete a match together via a room code. The user play-tests this; give them exact steps.
- The full-game wasm build matches native on the golden record, and that check is in the test scripts.
- Background-tab behaviour is defined and tested.

## Outcome

*(fill in when done)*
