# 12 — Browser netplay

- **Repo:** BitRiot (the vnet changes needed go to the vnet repo)
- **Branch:** `netplay/12-browser`
- **Depends on:** 07, 08 (screens and settings file), 09 and 10. Brief 11 is optional; if it has landed, use `RELAY_AUTO`. Both the BitRiot and vnet repos must be attached.
- **Status:** todo

## Goal

Browser players and native players join the same Bit Riot match through the vnet relay using a room code. The lockstep netplay from brief 07 runs unchanged over the relay backends. Brief 05b has already proved that the sim alone is bit-identical in wasm. This brief confirms that the full game build is too.

## Read first

- The Outcome sections of briefs 07, 09 and 10.
- [`README.md`](README.md): D6 and the network notes.
- Brief 05b's Outcome, for the gate results and the emsdk setup.

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
4. **Full-game determinism check.** Brief 05b already proved the sim under Node. Here, confirm that the *full game's* wasm build agrees: replay the golden record through the game with a Playwright test hook, and check it matches native checkpoints. Add this to the test scripts. If it fails, stop and report the first diverging tick and a dump diff.
5. **Background tabs.**
   - Browsers stop `requestAnimationFrame` in hidden tabs. Timers are throttled to roughly 1 Hz, and more heavily after about 5 minutes. A backgrounded peer would therefore stall everyone, and "step from a timer" does **not** keep up.
   - Options that do work:
     - **(a)** Drive stepping from incoming network messages. Message events aren't throttled.
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
