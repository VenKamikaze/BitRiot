# 12 — Browser netplay

- **Repo:** BitRiot (the vnet changes needed go to the vnet repo)
- **Branch:** `netplay/12-browser`
- **Depends on:** 07, 08 (screens and settings file), 09 and 10. Brief 11 has **landed**, so the browser uses `RELAY_AUTO`. Brief 14 (vnet 0.4.0, tag `v0.4.0`) also landed: `RELAY_AUTO` now works natively too (one `vnet_config` with `relay` and `relay_udp` for both worlds), `vnet_status_info` has `relay_rtt_ms` and `max_members`, and the relay client survives a hidden tab or a sleeping laptop. Read vnet's `doco/integration.md` and brief 14's follow-ups for 12. Both the BitRiot and vnet repos must be attached.
- **Status:** done (play-test pending)

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

**Status: done, play-test pending.** A native window and any number of browser tabs can be in one Bit Riot match through a vnet relay, by room code. **No wire change** (protocol version still 2: the work sits around `LockstepSession`, not in the messages), **no `bitriot_sim` source changed** (build id `0x6767612d056ba8af` on GCC and Clang, the six golden digests and `tests/data/golden-round.brmr` untouched), **no vnet change** (the submodule stays at `v0.4.0`, eb1feeb; nothing in vnet needed fixing, so there is no vnet commit or tag to make). Local play, direct `--host`/`--join`, `bitriot_headless host|join`, the multi-process tests, the `BITRIOT_NETPLAY=OFF` build and the web smoke test all still pass. What was *not* possible in the container: a person playing, real Firefox, a public relay, and a truly hidden tab (see "Background tabs").

**What landed**

1. **Netplay in the web build.** `BITRIOT_NETPLAY` (AUTO) is now ON for Emscripten when the game is built (still OFF for MinGW and for an Emscripten build of only the simulation, such as the determinism gate's). vnet builds there with its WebSocket/WebTransport backends (`-lwebsocket.js`, `-fwasm-exceptions` from the global flags). `HeadlessNet` is not built for the browser.
2. **Relay sessions.** `NetGame::Params::relay` (`relayWs`, `relayUdp`, `room`, plus development knobs `relayWt`, `relayCertSha256`, `impair`): one `vnet_config` with `VNET_BACKEND_RELAY_AUTO` for both worlds, as vnet's `integration.md` recommends. The host creates a room and listens on `netplay::RELAY_SESSION_PORT` (7777); a joiner reaches it at `room-host:7777` (the room creator is `10.0.0.1`). `NetGame::roomCode()`, `transport()`, `relayRttMs()`; `describeRelayFailure` words each relay error (no such room, room full, a limit, cannot reach the relay, the relay ended the session). A native build looks the relay's name up on the worker thread before creating the context (a numeric address needs none); a browser has no resolver, so `NetResolve` answers inline there.
3. **`LockstepSession`, three small options, no wire change** (kept apart for brief 13 to merge): `SessionOptions::maxDelay` (D clamp, default 8 as before, 12 for a relay: two hops); `tolerateSparsePump` (if this machine's own `pump()` calls are more than 1.5 s apart, nobody is timed out for it; on in the game and in `bitriot_headless`); and in `pump()` a peer's connect clock does not run while the transport is still CONNECTING (a relay that is slow to answer was otherwise reported as "the host did not answer"). Plus `hostSilenceMs()` and `sparsePumpGaps()` getters. `chooseDelay` gained a defaulted `maxDelay` argument.
4. **UI.** The main menu has one new entry, **Online game**, that opens `netmenu.rml`: **Host via relay**, **Join by room code** and, natively only, 08's **Host direct (UDP)** and **Join direct (UDP)** (a browser removes those two). Seven lines in the main menu would not fit the smallest window, which is why it is a sub-menu (and 08's play-test steps now start with Online game). New pages `nethostrelay.rml` (name, relay, players here) and `netjoinrelay.rml` (name, room code, relay, players here). The lobby title becomes **Room XXXXXX** in large yellow type, with a **Copy room code** button (SDL clipboard, and the page's clipboard in a browser), and the web page shows the code again as selectable text under the canvas with **Copy code** and **Copy invite link** buttons (`?room=CODE` pre-fills the join form). While the host's tab is quiet a peer sees "The host is away (nothing heard for N s)" (lobby) or "the host is away" in the round's overlay. The command line: `BitRiot --host --relay ADDR` opens a room (it prints `room CODE (transport)` on stdout), `BitRiot --relay ADDR --room CODE` joins; `bitriot_headless host|join --relay HOST:PORT [--room CODE] [--room-file F]`.
5. **Relay address text.** One field. `relay.example.org` or `host:port` is the relay's UDP address and the WebSocket URL is derived (`wss://host/vnet` when the page is https, else `ws://host:8080/vnet`, the relay's own defaults); `wss://host/vnet` or `ws://host:8080/vnet` is kept as the URL and the UDP address is that host on 7777 (`parseRelayAddress`). Default: the build's `BITRIOT_DEFAULT_RELAY` if set (a CMake cache string, compiled in as a define), else `127.0.0.1:7777` natively; a web page offers `?relay=`, else `window.BITRIOT_RELAY` (a hosting page sets it), else the page's own server (`wss://host/vnet` or `ws://hostname:8080/vnet`).
6. **Settings on the web: localStorage, not IDBFS.** `NetPrefs` gained `relay=` and `room=`. The game keeps reading and writing 08's `netplay.txt` in its in-memory file system; the page copies that text from `localStorage["bitriot.netplay"]` into the file before the first read and back after each write (`EventHandlerNetForm::setPrefsSyncHooks`). It is synchronous, needs no `FS.syncfs` choreography, no `-lidbfs.js`, and a blocked storage just means defaults.
7. **Hidden tabs** (below).
8. **Web hooks gated.** Every hook in `src/web/WebHooks.cpp` (the old ones too) answers only on a page opened with `?test=1` (or `Module.bitriotTest`); 09 asked for this before a public page. `?autostart` needs it as well. New hooks: `_bitriot_net_start/_room/_transport/_state/_press_start/_log/_info`, `_bitriot_replay_golden`, `_bitriot_hidden_polls/_is_hidden`; strings the page hands to the game go through `Module.bitriotTestArgs`. Page parameters: `?relay=` and `?room=` work on any page (the relay is on the form for all to see; an explicit `?relay=` beats the saved one). The dev knobs `?relay_wt=` and `?relay_cert=` (an explicit WebTransport URL and a development certificate hash), `?impair=` (a vnet impairment spec; a browser has no `VNET_IMPAIR`) and `?hiddendriver=off|timer` work **only with `?test=1`** (review fix S5: a crafted link must not route a visitor through another relay or degrade their connection). `-DBITRIOT_WEB_TEST_FILES=OFF` leaves the 39 KB golden record out of a deployed build.
9. **Deployment note**, below.

**Full-game determinism (item 4): wasm matches native.** The page hook `_bitriot_replay_golden` runs `replayRecord` on the *game's own* `Simulation` in the wasm game build (`GameEngine::replayRecordFile`) over `tests/data/golden-round.brmr` (made natively; preloaded into the web build at the same path): **2768 ticks, 2769 checkpoints (one per tick) verified, final hash `0x37140a4974ab41f6`, round over, no divergence**, the same as `bitriot_headless replay` prints. The networked rounds below are the second leg: wasm peers and a native host agree on every checkpoint of rounds started through the game's own lobby, `resetNetGame` and `NetMatch`.

**Background tabs (item 5): decision (a), network events, plus a one-second timer.**

- *How.* vnet's wake callback (called from the WebSocket or WebTransport event that queued a datagram) posts a message through a `MessageChannel`; its handler calls `webHiddenPoll()`, which runs one pass of the ordinary main loop with the engine in **hidden mode** (`GameEngine::setHidden`, set from `visibilitychange`): it draws and presents nothing, and a network game steps every tick it holds (the frame loop's `extraTicks` returns its maximum; each step is still gated by `canStep()`), not waiting for the clock, because the visible machines set the pace. A one-second `setInterval` does the same for vnet's own deadlines (retries, keepalives, the fallback timer), as vnet's `integration.md` says it must. A `MessageChannel` message is not throttled in a background tab; a timer is (about 1 Hz, and less after five minutes), and so is `emscripten_async_call`, which is a `setTimeout`: that is why the wake does not use it. Coming back, `rAF` resumes and the next frame redraws.
- *Measured.* A visible native-paced match with one browser tab made to look hidden *and* given Chromium's throttling (no `requestAnimationFrame`; `setTimeout`/`setInterval` held to at least one second; WebSocket and `MessageChannel` events untouched): **wake-ups plus timer: 30.4 ticks/s in the hidden tab and 30.3 in the visible one (the nominal 30); the one-second timer alone: 4.0 ticks/s (so the match crawls); nothing driving it: 0 ticks/s and the visible tab said "waiting for hidden" until the host dropped the slot to a bot after its 10 s of silence** (so the brief's fallback "slot back to AI after a grace period" is brief 07's existing leave path, which already does it; no new notification was needed). (**Corrected in the review fix round:** these numbers were taken with D forced to 8, which hid a real defect. The 10 ticks/s seen at D=2 on a busy machine was not "D too small for a hidden tab's longer round trip": a hidden pass stepped ticks but the inputs it sampled were only sent at the *next* `pump()`, i.e. the next network wake, so a hidden tab spent an extra tick interval of D. Fixed by flushing the session after the steps; see "Review fix round" for the numbers at D=2.) Two tabs that only *report* hidden (the `hidden` scenario, where `requestAnimationFrame` still fires) with a native host paced at 4 ms a tick finish a 5188-tick AI-heavy round in 20.8 s, 250 ticks/s, every checkpoint equal; **that figure includes animation frames and says nothing about throttled tabs: only `background` is a hidden-tab measurement** (review N5). If *all* machines are hidden nothing sets the pace, so such a match runs as fast as the round trips allow; harmless, and a match with nobody watching is not a case worth a clock.
- *What the tests did instead of really hiding a tab (the brief asks to say).* Playwright cannot hide a tab: it makes every page believe it is focused and visible (`Emulation.setFocusEmulationEnabled`), and in headed Chromium under Xvfb a second tab, a popup, a minimised window and `Page.setWebLifecycleState` all left `document.visibilityState` at `visible` (tried through Playwright and through a bare CDP connection). So the tests (a) override `document.hidden` and `visibilityState` and dispatch `visibilitychange` (the `hidden` scenario), (b) additionally impose the two throttling effects above (the `background` scenario, where the numbers come from), and (c) block a page's main thread for 5 to 6 s (the `frozen` and `host-away` scenarios), which is what a frozen tab or a laptop lid looks like to the game. **A real throttled tab in a real browser is part of the play-test** (steps below).
- *A hidden host* is not special: it is stepped by the guests' messages. If it is blocked altogether (the `host-away` scenario, 5 s), guests see "the host is away" within 1.5 s, the match resumes when it is back (`sparsePumpGaps` shows the host forgave its own gap) and every shared checkpoint still agrees. A host gone for more than 10 s is gone, as before (no host migration).

**Tests** (GCC Release; the same on Clang Release)

- `bitriot_tests` 678 -> **700**, `bitriot_sim_tests` 532 -> **545**; `-DBITRIOT_NETPLAY=OFF`: **526** and **412** (unchanged). ctest 3/3 (the process test now has 11 cases: 3 new through a real relay, about 21 s more). New unit tests: `RelayAddress` (4), `RoomCode`, `RelayPrefs`, `RelayFailure`, `RelayLobby` (2), `LockstepRelay` (4), `NetGameRelay` (1), `LobbyLayoutTest` (3: the online menu, the room-code lobby, the relay failure pages), `NetFormTest` (3), `GameOptionsTest` (2).
- `tests/web/netplay.js` (below; about 15 minutes in all): `golden` (7 checks), `ws`, `wt`, `ws-impaired`, `wt-impaired` (a native `bitriot_headless host --relay` and two browsers, scripted key presses, a whole round each: **every checkpoint hash and the winner agree**; the rounds were 2085 ticks over WS, 4117 over WT, 3705 over WS with `lat=40,jit=15,loss=4,dup=1,reorder=2` in the browsers and `lat=30,jit=10,loss=4,dup=1` natively, 1675 over WT with the same; the impaired host chose D=6 from a 127 ms round trip, the clean ones D=2), `hidden`, `background`, `frozen`, `host-away`, `ui`, `errors`. `tests/web/smoke.js`: **41 of 41** (its URL has `?test=1` now, and its Exit click moved to the fourth menu line).
- **Proof that they fail without the change**: one-at-a-time source mutations, each rebuilt, run, restored (the list is in `tests/README.md`): all killed. For the web ones: the wake handler emptied gives 1.0 ticks/s in `background` (three checks fail); the hidden free-run removed gives 11.8 ticks/s there and 30 ticks/s in `hidden`, which showed that check's first threshold (30) proved nothing, so it is 100 now. The golden hook is itself the change (without it the scenario has nothing to call).
- **Sweep** (200 seeds, GCC Release, from the build directory, three at a time, nothing else rebuilding): see "Final numbers" below.

**Decisions** (the brief left them open; the recommended option was taken)

1. **Strategy (a)**, as above; no Worker timer (b) (a second thread of timers would still be throttled in some browsers and adds a build mode) and no audio trick (c). The fallback to AI is brief 07's 10 s rule.
2. **localStorage, not IDBFS** for the settings (above).
3. **No default public relay.** `BITRIOT_DEFAULT_RELAY` is empty, so a native build offers `127.0.0.1:7777` and a web page the server it came from. Which relay to run is the user's decision.
4. **One host:port/URL field**, parsed as above, rather than two fields.
5. **A sub-menu**, because seven lines overflow the menu window.
6. **Impairment "at the relay"** means vnet's impairment on every machine's sends (`?impair=` in the browsers, `VNET_IMPAIR` natively, each with its own seed), which is where vnet has it: the relay has no impairment flag. Both WebSocket and WebTransport are covered.
7. **D up to 12 over a relay**, 8 otherwise.
8. **Round length in the tests.** A human standing still makes AI rounds with the spawner off last 7000+ ticks; the tests use `players=4,map=13x11,blocks=30` with the spawner on (1000 to 4000 ticks, 40 to 140 s at the nominal pace) and the 4 ms host for the long AI-heavy one.
9. `std::filesystem` and `/dev/urandom` (07's follow-ups) needed nothing: libc++'s filesystem compiles and `NetRandom` already falls back to the clocks when `/dev/urandom` cannot be opened (Emscripten has one anyway).

**Deviations, and corrections to the brief and code-facts**

- The brief's "Add 'Host via relay' and 'Join by room code' to brief 08's screens": they are on a new page behind **Online game**, so brief 08's two main-menu entries moved one level down on native; code-facts §8 and 08's play-test steps are updated for it (the steps below are what to use).
- Code-facts §8/§12 said brief 07/08's follow-ups for 12 were `std::filesystem`, `/dev/urandom`: not problems (decision 9). code-facts has a new brief 12 paragraph and header note.
- The brief says Playwright can test with `Page.setWebLifecycleState` and/or visibility overrides: the lifecycle call did not freeze the headless page here, and neither it nor anything else hid a tab (see Background tabs); the tests use the overrides, imposed throttling and a blocked thread, and say so.
- `bitriot_headless join` no longer requires `HOST:PORT` first when `--relay` is given.
- The web hooks of brief 09 are gated behind `?test=1` now, which changes `tests/web/smoke.js` (its URL).

**Follow-ups**

- **For 13 (disconnects and hot-join).** The merge points in `LockstepSession`: three new `SessionOptions` fields, `chooseDelay`'s extra argument, the block at the top of `pump()` (sparse-pump forgiveness and the connect clock hold), `hostSilenceMs()`/`sparsePumpGaps()`, and `computeDelay` using `m_opt.maxDelay`. No wire or `protocol.md` change here; 13 owns `PROTOCOL_VERSION` 3 and nothing in 12 needs to be reconciled except that text. A reconnecting browser is a new vnet context with a new virtual address (integration.md): identify by the token, not the address. The "host is away" sentence reads `hostSilenceMs()`.
- **D for a tab that goes hidden.** The host chooses D once per round from round trips a tab measures while visible; a tab hidden later answers a little more slowly, so it plays at about D/RTT. With the input flush it keeps 28.6 ticks/s at D=2 and 25 ms latency; a host that re-measures for a later round is possible but not needed.
- **A hot-joiner over a relay** needs nothing new from the relay (late joiners already enter a room).
- The room code has 30 bits and no authentication (vnet's own note): anyone with a code can join a lobby until it is full or the host starts. A host-side "lock" and a join password are possible later.
- **Copy room code from the in-game button in a browser** uses `navigator.clipboard` from a click that RmlUi handles a frame later; the page's own Copy buttons (real DOM clicks) are the reliable ones, and are tested only for presence (headless Chromium denies clipboard writes), not for the clipboard's content.
- **Keys stuck after switching tabs** (a key held while the tab is hidden): SDL clears keyboard state on focus loss; not exercised.
- Mobile, a server browser, WebTransport behind a general reverse proxy: out of scope as the brief says.
- Ctrl+W (Player 1's weapon key) closing the tab is still 09's open point.

**Deployment note (item 6; nothing was deployed)**

*What to host.* Two things: the static game files (`build-wasm/index.html`, `BitRiot.js`, `BitRiot.wasm`, `BitRiot.data`; about 8.5 MB, 3 MB compressed) and a `vnet-relay`. Build the game with `-DBITRIOT_WEB_TEST_FILES=OFF`. (`-DBITRIOT_DEFAULT_RELAY=...` is for native builds only; a page derives its relay from where it was loaded, or from `window.BITRIOT_RELAY = "wss://relay.example.org/vnet";` set in a `<script>` before the game's script, or from `?relay=`.) The page must be **HTTPS** (a secure context) to use `wss://` and WebTransport; plain `http://localhost` also counts as secure for development. Serve `.wasm` as `application/wasm` and `.data` as binary; gzip or brotli them. No COOP/COEP headers (the build has no threads).

*Choices for the user.* The static files can sit on GitHub Pages (or any static host; it is HTTPS already) while the relay needs a small always-on machine with a public name and UDP and TCP ports: a cheap VPS (1 vCPU is enough for a few hundred players; `relay/README.md` has the sizing). The relay's own TLS comes from Caddy or nginx in front of it for `wss://` (its README has both snippets); WebTransport needs a real certificate for the relay's name (Let's Encrypt) and UDP 443 or 4433 open, and the relay terminates QUIC itself. Everything works with the WebSocket alone, which is the fallback anyway.

*Steps (a relay at `relay.example.org`, the game at `https://you.github.io/bitriot/`).*

1. On the VPS: install Go 1.24, `git clone --recurse-submodules` the game (or just vnet), `cd relay && go build -o /usr/local/bin/vnet-relay ./cmd/vnet-relay`, make a user, a systemd unit running `vnet-relay -udp :7777 -http 127.0.0.1:8080 -trust-forwarded-for -origins you.github.io` (add `-wt :443 -wt-cert fullchain.pem -wt-key privkey.pem` for WebTransport, see its README for the certificate copy).
2. Caddy: `relay.example.org { reverse_proxy /vnet 127.0.0.1:8080 }` (certificates automatic). Open TCP 443 and UDP 7777 (native players) and, for WebTransport, UDP 443 or 4433.
3. Build the web game (`scripts/setup-emsdk.sh`, `emcmake cmake -S . -B build-wasm -DBITRIOT_WEB_TEST_FILES=OFF`, `cmake --build build-wasm`), copy `index.html`, `BitRiot.js`, `BitRiot.wasm` and `BitRiot.data` to the static host. Edit the copied `index.html` to add `<script>window.BITRIOT_RELAY="wss://relay.example.org/vnet";</script>` before `{{{ SCRIPT }}}`'s script (or hand out links with `?relay=wss://relay.example.org/vnet`).
4. Check: `curl https://relay.example.org/healthz`; open the page, Online game, Host via relay: a room code appears. A native player: `BitRiot --relay relay.example.org --room CODE` (UDP 7777).
5. Things to know: everyone behind one NAT shares the relay's per-address limits (128 members, 16 rooms); a relay restart ends every room; a dev certificate (`-dev-cert`) is for local trials only; `-origins` keeps other sites from using the relay.

**Review fix round** (an Opus review of c956879: merge after fixes; new commits on top, no history change)

- **S1, inputs flushed after the steps (this was the real cause of the 10 ticks/s).** `GameEngine::flushNet()` (a `pump()` of the session) runs from `loopIteration()` whenever the frame stepped a tick, natively and in the browser. `background` now runs at **forced D=2** with `lat=25` on the browsers, so it guards it. Hidden tab, wake-ups mode: before the fix 24.5 and 23.9 ticks/s (the reviewer's figures at D=2, lat=25; 8.3 ticks/s in my own mutation run of the same scenario with the flush commented out, three checks fail); **after: 28.6 (hidden) and 28.7 (visible)**; the reviewer's lat=12 figures were 23.5/24.6 before and 30.3/30.3 after. The earlier "30.4 at D=8" hid it.
- **S2.** `setHidden()/hidden()/netLog()` and their members no longer sit under `#ifdef BITRIOT_NETPLAY`; `-DBITRIOT_NETPLAY=OFF` for the web builds, compiles, and `node tests/web/smoke.js build-wasm-off --menu-lines 3` passes **41 of 41** (the Exit click depends on the menu's line count, so the smoke test takes `--menu-lines`).
- **S3.** Hidden passes run only while a network game (or a relay lookup) exists: `GameEngine::hiddenPassesWanted()`. New scenario `hidden-local`: a local round in a throttled hidden tab does not advance (ticks 0 in 10 s, no sound, no hidden pass). Mutation (`return m_hidden;`): ticks 42 to 92, sounds 10 to 17, fails.
- **S4.** An explicit `?relay=` wins over the saved relay (`setRelayDefaults(..., relayIsExplicit)`); a relay equal to the page's default is never saved (so a site that changes `window.BITRIOT_RELAY` moves its returning visitors); the invite link is built from the relay the game is using (`Module.bitriotRoom(code, relay)`, `GameEngine::netRelayText()`). `NetFormTest.TheRelayFormsFill...` covers explicit precedence and the not-saved default; scenario `knobs` joins through the real form with a *different* relay saved in localStorage, using an invite-style `?relay=&room=`, and checks the relay the page reports. Mutation (explicit ignored): "never reached net state 2,3", fails.
- **S5.** `relay_wt`, `relay_cert`, `impair` and `hiddendriver` are read only with `?test=1`; `bitriot_dev_knobs_active()` (always on) says whether any took effect. Scenario `knobs`: 0 without `?test=1` and the test hooks shut too, 1 with it. Mutation (gate removed): fails.
- **N1.** `NetGame::sessionOptionsFor(params)` is the one place the game's session options are made (tested: D limit 8 direct and 12 on a relay, sparse-pump rule on); the process test `OverASlowRelayPathTheHostChoosesADBeyondEight` (peer sends with 120 ms latency) makes the *headless* host choose D above 8 (it failed with `so.maxDelay = 12` removed from `HeadlessNet.cpp`: "8 vs 9").
- **N2.** New scenario `asleep`: a browser host and a browser guest both blocked for 12 s (more than the 10 s timeout): both are still in the round, each counted a pump gap, and the match plays on at speed. With `tolerateSparsePump = false` in `sessionOptionsFor` it fails (the guest ended with "lost the connection to the host", end reason 3, and no gaps were counted).
- **N3, N4.** `protocol.md` now says the D clamp is `maxDelay` (8; 12 on a relay), what the `+ 1` assumes and why a tab hidden after the pings plays at about D/RTT, the connect clock being held while the transport is CONNECTING, and the sparse-pump rule with its two edge cases (a tab under Chrome's intensive throttling never times a dead host out while hidden and finds out within 10 s of being pumped normally again; a host that hiccups for more than 1.5 s at least every 10 s keeps resetting its peers' silence clocks, so a really dead peer is dropped later). Docs only: the protocol version is still 2.
- **N5.** See the corrected note under Background tabs.
- **Test counts:** GCC and Clang Release `bitriot_tests` **701**, `bitriot_sim_tests` **545**; `BITRIOT_NETPLAY=OFF` 526 and 412; ctest 3/3 (process test: 12 cases, 137 s); smoke 41 of 41 with and without netplay (web); `tests/web/netplay.js` all 14 scenarios, **194 of 194** checks (the one failure in the full run, the `ui` scenario's old "relay was saved" check, was the S4 change working; the check now says the default relay is not saved, and `ui` passes 9 of 9); 200-seed sweep on GCC Release (three at a time, tree not rebuilt): **0 failing seeds** on both binaries.

**Merged with 13** (brief 13, protocol version 3, merged into this branch as af80165; 12 kept its wire-neutral options, 13's wire is the wire)

Conflicts and how they were resolved: `GameOptions`/`NetGame.h`/`EventHandlerNetLobby.h`/`main.cpp`/`GameEngine.h`/`HeadlessNet.cpp` (both sides added options, parameters or members: both kept); `NetGame.cpp` (13's `reconnectToken`/`spectate` moved into 12's `sessionOptionsFor`); `GameEngine.cpp` (13's `catchingUp()` gate on `canStep` combined with 12's hidden free-run; 12's `adoptNetGame` now also clears 13's `m_netPeersKnown`/`m_netTokenSaved`; `netJoinFrame` and `logNet` both kept, and `logNet`'s round-over line moved into 13's `netRoundMaybeOver`); `LobbyModel` (`NetPrefs` has `relay`, `room`, `token`, `tokenHost`); `NetplayProcessTest` (both appended sets kept); docs (README rows: 12 and 13 both `done (play-test pending)`; `protocol.md`: 13's version-3 text with 12's `maxDelay`, sparse-pump and connect-clock paragraphs; `tests/README.md` both sections).

Integration, checked and changed:

1. **Sparse pumps and 13's timers.** 13's host-side clocks (`readyAtSince`, the wait for a peer's last checksum from `m_roundEndedAt`, the pending-token grace) run on the host's own pump clock, so a host that was not pumped for a while would have timed a joiner or peer out for it. The sparse-pump block now moves them on by the gap (new test `LockstepRelay.AGapInTheHostsOwnPumpsDoesNotRunOutThirteensFinalChecksumWait`; with the `m_roundEndedAt` shift removed it fails). `TOKEN_QUIET_MS` (3 s) is above the 1.5 s gap bound, and after a gap the quiet clock restarts from the resume (`lastHeard` is set to now), which is the safe direction. A joiner replaying the record is pumped every frame, sends its acks after each slice, and so is never silent for the host; a hidden joiner is driven by the same network wake-ups as anything else.
2. **`flushNet()` and the fast-forward.** `netJoinFrame` now pumps the session after each fast-forward slice (the acks and `READY_AT` go out at once), and `loopIteration`'s flush covers the ordinary steps. The hidden-tab driver drives a joiner's catch-up with no extra code: `hotjoin-hidden` (a throttled hidden browser joining a running native round) catches up in 5.8 s and then plays at 30.3 ticks/s. (I first added a "ask for another pass at once while catching up" request; the same run gave the same 5.8 s with and without it, so it was removed rather than kept unproven.)
3. **D = 12 and `TAKEOVER_TOO_SLOW`.** 13's `delayNeededBy` clamped what a joiner needs with `chooseDelay`'s default limit of 8, so on a relay round with D = 12 a machine that needed 26 looked fine. It now asks for the unclamped need. New test `LockstepRelay.OnARelayWithADOfTwelve...` (a joiner at 150 ms each way, needing 11, is granted a slot; one at 400 ms, needing 26, is told TOO_SLOW; with the old clamp the second fails).
4. **Browsers.** Two new scenarios: `hotjoin` and `hotjoin-hidden` (a browser joins a native host's running round by room code, replays it, is granted a bot, plays at the nominal pace and every checkpoint it stepped agrees with the host's, same winner) and `reconnect` (a tab that closes and comes back by room code with its saved token gets the same slot *and the same token*, which a stranger would not; with the token not passed it fails: the tokens differ). To make that work the saved token is now keyed by the relay **and the room** (`"relay (room CODE)"`) and passed on relay joins from the forms, the command line and the web hook; before, only direct joins used it. Not done: a reconnect while the old relay session still lingers (UDP members linger about 15 s; a WebSocket closes at once, so the scenario cannot see it), and a reconnect that lands on a different virtual address is only covered by 13's loopback tests and process test (the token is looked up, not the address).

**Final numbers**

(GCC 13 and Clang 18 Release, this branch; `bitriot_headless version`: build id `0x6767612d056ba8af`, data hash `0x0ca2017527be6327`, both unchanged.)

- GCC Release: `bitriot_tests` **700** pass, `bitriot_sim_tests` **545** pass. Clang Release: **700** and **545**. `-DBITRIOT_NETPLAY=OFF` (GCC): **526** and **412**. ctest (GCC): **3/3**, the process test 96 s (11 cases).
- 200-seed sweep (`BITRIOT_TEST_SEED=1..200`, GCC Release, from the build directory, three seeds at a time, on a machine that was also being used by another build): `bitriot_tests` **0 failing seeds**, `bitriot_sim_tests` **0**. No simulation source changed, so there is no "before".
- Web: `tests/web/smoke.js` **41 of 41**; `tests/web/netplay.js` all scenarios **170 of 170** in the end: the first complete run gave 168 of 170, the two failures being the `background` scenario's wake-up check at **10.0 ticks/s** on a busy machine (see above); it reproduced at 10.1 in the single-scenario rerun, so it was read as a cause, not as noise: D was too small for the hidden tab's longer round trip, the scenario then forced D=8 (30.4 ticks/s); the review fix round found the real cause and the scenario now runs at D=2. (Superseded by the review fix round: the cause was not D; see below.)
- The long rounds, for reference: WS 2085 ticks, 70 checkpoints, 68.8 s; WT 4117 ticks, 138 checkpoints, 135.9 s; WS impaired 3705 ticks; WT impaired 1675 ticks; hidden AI-heavy 5188 ticks, 173 checkpoints, 20.7 s.
- The real game under Xvfb (native, `SDL_AUDIODRIVER=dummy`, screenshots; evidence, not a play-test): the Online game menu with its five lines fitting the window, the Host via relay form with the relay prefilled, and the lobby `Room 2BBMRK` with the Copy room code button against a local relay. Not driven under Xvfb: a second native window joining by code (the process test and the browser tests cover the join), controllers, Firefox.

**Play-test** (pending). Arch Linux, a native window and a browser tab through a relay on your own machine, Chrome and Firefox. The setup of 07 (packages, RmlUi 4.3) and 09 (emsdk) applies.

```bash
sudo pacman -S --needed go                      # for the relay
git clone --recurse-submodules --branch netplay/12-browser https://github.com/VenKamikaze/BitRiot.git ~/bitriot-12   # or `claude` once merged
cd ~/bitriot-12
(cd ext/vnet/relay && go build -o ~/vnet-relay ./cmd/vnet-relay)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF && cmake --build build -j"$(nproc)" --target BitRiot
scripts/setup-emsdk.sh && source .emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm && cmake --build build-wasm -j"$(nproc)"
~/vnet-relay -udp :7777 -http :8080 -v &         # terminal 1: the relay
python3 -m http.server -d build-wasm 8000 &      # terminal 2: the game's files
```

1. **Native hosts, browser joins.** Native (from `build/`): `./BitRiot` -> *Online game* -> *Host via relay* -> name `Alice`, relay `127.0.0.1:7777` -> *Open room*. The lobby title reads `Room XXXXXX` in big yellow letters and the terminal printed `room XXXXXX (relay-udp)`. In Chrome open `http://localhost:8000/`, *Click to start*, *Online game* -> *Join by room code*, type the code (any case, the cursor starts in the code field), name `Bob`, relay `ws://localhost:8080/vnet` (that is also what the page offers by itself), *Connect*. Bob's lobby shows `Room XXXXXX` and Alice's row; press *Ready* (Return); Alice presses *Start match*. Play: WASD and Left Ctrl in each window (each machine's first player uses keyboard set 1). Both pictures should stay identical; the title/status line reads `NET host D=n relay-ws ...` (n is probably 2 to 4 on one machine); watch for the red "Waiting for ..." box.
2. **Browser hosts, native joins.** Browser: *Online game* -> *Host via relay* -> *Open room*. The code appears in the game *and* under the canvas as text: use **Copy code** or **Copy invite link**. Native: `./BitRiot --relay 127.0.0.1:7777 --room CODE --name Alice` (or the menus). The browser host presses *Start match* once Alice says ready.
3. **Firefox.** Repeat 1 with Firefox as the browser (WebSocket; Firefox has WebTransport but a dev certificate is only needed to test that; skip it). Open the invite link from step 2 in a second tab or second browser: the join form comes up with the code filled in.
4. **A really hidden tab (the measurement the container could not make).** With a native host and a browser guest in a round, switch to another tab for a minute and back. Expected: the match never shows "Waiting for" on the native side for more than a moment and the guest has kept up (its picture is current at once, the HUD tick advances). Then do it for five minutes (Chrome's intensive throttling). Then host from a browser, hide *its* tab, and watch the native guest: it should keep playing. If a tab is hidden with the *only* other player on the same machine the pace is whatever the round trips allow (fast); that is expected.
5. **The host's tab frozen.** Browser hosts, native guest, a round running. In Chrome open `chrome://discards` and use *Freeze* on the host's tab (or suspend the whole browser with `kill -STOP` on its main process and `kill -CONT` it later). The guest should show "the host is away" and, if the tab comes back within 10 s, play on; after 10 s it says the connection to the host was lost.
6. **Errors.** A wrong code (`ZZZZZZ`): *No such room*. Stop the relay and press *Open room*: *Cannot reach the relay*. Close the host's lobby while the guest is in it: "The host left".
7. **Saved settings.** Reload the page: *Join by room code* has your name, the last room and the relay filled in (stored in the browser's localStorage; clearing site data resets it).
8. **Impairment (optional).** `http://localhost:8000/?impair=lat=60,jit=20,loss=5,seed=1` for the browser and `VNET_IMPAIR="lat=60,jit=20,loss=5,seed=2" ./BitRiot ...` natively: D goes up (the status line shows it), input is laggier, the match still ends with identical pictures.
9. **Regression.** `./BitRiot` with no flags: menu, local round, scoreboard. *Online game* -> *Host direct (UDP)* and *Join direct (UDP)* still do what 08's steps say. The scoreboard after a relay round: the host's *Next round* and *Back to lobby*, a peer's *Waiting for the host...*.

Record the result here when it comes back.
