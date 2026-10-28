# 12 — Browser netplay

- **Repo:** BitRiot (the vnet changes needed go to the vnet repo)
- **Branch:** `netplay/12-browser`
- **Depends on:** 07, 09 and 10. Brief 11 is optional; if it has landed, use `RELAY_AUTO`.
- **Status:** todo

## Goal

Browser players and native players join the same Bit Riot match through the vnet relay using a room code. The lockstep netplay from brief 07 runs unchanged over the relay backends. Brief 05's determinism gate is extended to prove that native and wasm simulations are bit-identical.

## Read first

- The Outcome sections of briefs 07, 09 and 10.
- [`README.md`](README.md): D6. This brief completes the float gate.

## In scope

1. **Bit Riot on the relay**
   - Wire the netplay layer to vnet's relay backends:
     - the browser uses `RELAY_WS`, or `RELAY_AUTO` if brief 11 is in;
     - native uses `RELAY_UDP`.
   - The host creates a room; joiners enter the room code.
   - By relay convention (10), the host is `10.0.0.1`.
   - Direct native `--host`/`--join` over plain UDP stays available.
2. **UI**
   - Add "Host via relay" (it shows the room code big and copyable) and "Join by room code" to brief 08's screens.
   - Relay URL: a sensible default from build config, overridable in settings.
3. **Settings persistence on web:** the name, last room and relay URL use IDBFS or localStorage. Brief 08 introduced a settings file; pick the simplest option that works with it.
4. **Cross-platform determinism gate (D6)**
   - Replay the golden record from brief 05 in a wasm build, either `bitriot_headless` under Node or the game with a test hook in Playwright. Every checkpoint hash must match native.
   - Add this to the test scripts.
   - If it fails, stop and report the first diverging tick and a dump diff. That triggers the fixed-point contingency, which needs a decision from the user.
5. **Background tabs**
   - Browsers throttle or stop requestAnimationFrame in hidden tabs. A backgrounded peer would therefore stall everyone.
   - Handle it as follows. On `visibilitychange` to hidden, keep stepping from a timer if that keeps up. Otherwise notify the host and let the slot fall back to AI after a grace period, using brief 07's leave/timeout path. Brief 13 makes reconnection smooth.
   - Document what happens so players aren't surprised.
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
- **The golden-record wasm replay** matches native (item 4).
- **Background-tab behaviour:** emulate a hidden page in Playwright and check the documented outcome.

## Acceptance criteria

- A native and a browser player complete a match together via a room code. The user play-tests this; give them exact steps.
- The native ↔ wasm determinism gate passes and is in the test scripts.
- Background-tab behaviour is defined and tested.

## Outcome

*(fill in when done)*
