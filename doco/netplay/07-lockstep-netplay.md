# 07 — Native lockstep netplay

- **Repo:** BitRiot. The vnet repo must also be attached to the session: it is consumed here.
- **Branch:** `netplay/07-lockstep`
- **Depends on:**
  - 05b: the determinism gate passed; `bitriot_headless`, the record format, the data hash and the build id (all from 05)
  - 06: vnet with native UDP, loopback and impairment; 14 (vnet 0.4.0): `vnet_set_impair`, burst loss, `vnet_wait`, `vnet_resolve_host` and the integration guide
  - Q4 is answered: the online player cap stays at 6, the same as local play
- **Status:** done (play-test pending)

## Goal

Two to six players on different machines play a Bit Riot match over the network. Each machine may also have local couch players.

- Every peer runs the same `Simulation`, and only `TickInputs` cross the network.
- Desyncs are detected within a second and produce usable diagnostics.

This brief is native-only and driven from the command line. The lobby UI is brief 08 and browsers are brief 12.

## Read first

- [`README.md`](README.md): decisions D1, D2, D5, D7 and D8.
- The Outcome sections of briefs 03a–06. Use the APIs as they actually landed, especially `frame()`/`canStep()` (03b), the local slot mask (04a) and `ControlEvent`s (04b).
- [`research/code-facts.md`](research/code-facts.md): §7 (settings to share) and §12 (scoreboard "Play Again", `setPointers`, `getopt`).
- vnet's [`doco/integration.md`](https://github.com/VenKamikaze/vnet/blob/claude/doco/integration.md) (per-frame sequence, status states, what to do on `FAILED`, `VNET_EAGAIN` from `vnet_sendto` is a drop) and the follow-ups for 07 in vnet brief 14's Outcome.

## Design (settled; refine the details)

**Host authority (D2).** The host is authoritative for:

- the final `TickInputs` of every tick, merging every peer's slots and control events;
- the match settings, seed and slot assignment;
- round start, round end and the next round.

**Input delay (D8).** A local input sampled at tick T is scheduled for T+D. A peer steps T only when it holds the host's confirmed `TickInputs` for T. Otherwise `canStep()` returns false, which stalls the peer, and it shows "waiting for <name>". Each `TICKS` packet tells peers who the host is still waiting on.

- **The host chooses D** from lobby `PING` statistics:
  - a peer's input needs a full round trip to come back confirmed, plus up to one frame of sampling and send phase;
  - so take D = `ceil((max RTT + jitter) / 33 ms) + 1`, clamped to 2..8;
  - send it in `START`, and log it;
  - over a relay (brief 12) there are two relay hops, so expect a higher D;
  - D=3 is only the default for tests and loopback.
- **Ticks 0..D−1** have no sampled input. They are implicitly confirmed as empty at `START`.
- **The host doesn't run ahead** of its slowest peer by more than D. Define exactly how it paces.

**Catch-up.** A peer holding more than D+2 confirmed but unstepped ticks may step up to 2 extra ticks per frame (`frame()`'s accumulator from 03b) until it is level.

**Sampling.** A peer samples once per stepped tick (04a). While stalled, local events accumulate into the next sample. **Only local slots are sampled**, using the local slot mask; remote humans must be excluded from the keyboard assignment in `setPointers` (code-facts §12).

**Loss tolerance (D7).**

- Every `INPUT` packet (peer → host) repeats all of that peer's unacknowledged ticks.
- Every `TICKS` packet (host → peer) repeats all confirmed ticks the peer hasn't acknowledged.
- Both piggyback an ack of the highest contiguous tick received.
- Packets are capped at `VNET_MAX_DATAGRAM`, sending the oldest ticks first.

**Messages.** All are versioned, with a protocol version in `HELLO`.

| Message | Content |
|---|---|
| `HELLO` | protocol version, **build id**, **data hash** (05), name, number of local players wanted |
| `WELCOME` | assigned slots, plus a reconnect token (used in 13) |
| `REJECT` | the reason. A mismatched build id is rejected even if the protocol version matches. |
| `LOBBY` | host → all: slot table (host / peer / local / AI, team) and settings |
| `START` | `MatchConfig` including the seed, D, first tick |
| `INPUT` | ack, first tick, then per tick this peer's slots' `PlayerInput`s and `ControlEvent` requests |
| `TICKS` | ack, first tick, then per tick the full `TickInputs`, plus who the host is waiting on |
| `CHECKSUM` | peer → host: tick, `stateHash()`, sent every 30 ticks |
| `DESYNC` | host → all: tick (see below) |
| `PING` / `PONG` | RTT measurement for choosing D, and for display |
| `KEEPALIVE` | sent in every state, including menus |
| `LEAVE` | explicit quit (ESC) |

**Session pumping in every state.**

- The session is polled and sends keepalives in `MENU_RUNNING` too: the scoreboard, between rounds and in the lobby. Otherwise a peer reading the scoreboard times out.
- **On peers, the scoreboard's "Play Again" is disabled or ignored** (`scoreboard.rml` sets `GAME_INIT` locally, code-facts §12). Only the host's `START` begins a round.

**Settings isolation.** Applying `MatchConfig` writes `GameSettings` (03a). A joining peer snapshots its local settings before the match and restores them when it leaves.

**Rounds.**

- The round ends on the same tick everywhere, because the round-end logic is part of the sim (01, 03a).
- Every peer shows the scoreboard.
- The host sends the next `START` with a new seed.

**Leaving and timeouts.**

- When a peer sends `LEAVE` or times out (no packets for ~10 s), its slots return to AI. The host schedules `RETURN_TO_AI` events on a future confirmed tick, and the match continues.
- **Host loss:** there is no host migration. Peers detect the host timing out, end the match with a clear message, and return to the menu.
- Reconnecting is brief 13.

**Local takeover of an AI slot mid-match.** The local join selector produces a `ControlEvent` request. The peer sends it to the host in `INPUT`, and the host schedules it on a confirmed tick.

**Desync diagnostics.**

- By the time `DESYNC` arrives, peers are already past the tick in question, and undefined behaviour might not replay the same way. So **every peer keeps a ring buffer of state dumps for its last K checkpoint ticks**, using brief 05's dump format.
- On `DESYNC`, every peer writes:
  - the dump for that tick;
  - its `MatchRecord` of the round so far.
- The match then stops with a message giving the file paths.

**Code layout.**

- Put the code in `src/net/`.
- **`LockstepSession` stays free of SDL and of `Simulation`.** It handles the protocol, buffering, acks, pacing and the choice of D, and exchanges `TickInputs` and hashes through a small interface. That lets it be unit-tested with N fake peers on vnet loopback in a single process, even though the singletons allow only one `Simulation` per process.

**Dependency.**

- Pull in vnet as a submodule at `ext/vnet` or through `FetchContent` with `GIT_REPOSITORY`. Justify the choice.
  - **vnet's state (Oct 2026):** briefs 06, 10, 11 and 14 are merged on vnet's `claude` branch. Release tags: `v0.1.0` (06), `v0.2.0` (10), `v0.3.0` (11) and `v0.4.0` (14, commit eb1feeb). Pin **`v0.4.0`**; check it exists with `git ls-remote --tags https://github.com/VenKamikaze/vnet`. 0.4 is source-compatible with 0.3, not binary-compatible: build vnet with the game.
  - vnet APIs worth using: `vnet_time()` (one backend-independent clock: virtual on loopback, so `LockstepSession` tests never sleep); the loopback backend with seeded impairment; `VNET_IMPAIR` with a distinct `seed=` per process, including burst loss (`burst=`, `blen=`) for blackout tests; `vnet_set_impair()` to change conditions mid-test (stall and catch-up, leave and timeout, host loss); `vnet_wait()` so `bitriot_headless` peers don't spin; and `vnet_resolve_host()` for `--join host:port` with a host name (it blocks: call it before the loop or on a worker). `vnet_resolve` itself takes `host:port` numerically (no DNS).
  - 03b's `FrameLoop` gives you `canStep()` (veto a tick) and `extraTicks()` (up to 2 extra ticks a frame, to catch up after a stall). Wrap `GameEngine`'s hooks, don't replace them; read 03b's Outcome follow-ups for 07.
- Update `scripts/cloud-setup.sh` as needed. vnet is public, so no credentials are needed to fetch it.
- Add a `BITRIOT_NETPLAY` CMake option: ON by default, and OFF on MinGW unless brief 06 verified that vnet builds there.

**Command line until brief 08.**

- Options: `--host [port]`, `--join host:port`, `--name <n>`, `--local-players N`. Switch to `getopt_long`.
- Both peers enter a text-mode or minimal lobby, and the host starts the match with a key.

## Tests

**`LockstepSession` unit tests**, with N fake peers on vnet loopback with impairment:

- identical, in-order `TickInputs` on every peer, at 0%, 5% and 20% loss plus duplication and reordering, at 80 ms latency;
- stall and catch-up;
- the choice of D from simulated RTTs;
- leave and timeout schedule `RETURN_TO_AI`;
- host loss is detected;
- version, build-id and data-hash mismatches are rejected;
- keepalives flow while no round is running.

**Multi-process integration.**

- 2–3 `bitriot_headless` processes, extended with `--host`/`--join`, over native UDP on 127.0.0.1, with impairment from `VNET_IMPAIR`.
- Use **ephemeral ports**, so parallel ctest runs don't collide.
- Play an AI-heavy round with scripted inputs to its end. All processes must report identical checkpoint and final hashes and the same winner. Then play a second round started by the host.
- Run it under ctest with a timeout. It may be excluded from the fast default suite if `tests/README.md` documents that, and the coordinator still runs it.

**Desync detection.** A test-only flag perturbs one peer. `DESYNC` must fire at the first checkpoint after the perturbation, and both peers' ring-buffer dumps and records must be written.

Prove that tests fail without the code where it makes sense: for example, session tests with redundancy disabled, run under loss.

## Acceptance criteria

- **Two real windows on one machine play a full match.** For example, `--host` and `--join 127.0.0.1:<port>`. This needs a user play-test, which may be pending at merge; give the user exact commands.
- **The multi-process headless test passes under impairment.**
- **Desyncs are detected,** and their dumps and records are written.
- **Peer loss turns that player into AI. Host loss ends the match cleanly.**
- **Local play is unchanged** when no netplay flags are given.
- **`doco/netplay/protocol.md`** documents every message and the versioning.

## Outcome

**Status: done, play-test pending.** Native lockstep netplay works end to end: a session library with its own protocol (`doco/netplay/protocol.md`), `bitriot_headless host|join`, a multi-process test over real UDP under loss, desync detection with dumps and records, and the game itself (`--host`/`--join`, a lobby page, the scoreboard, leaving). **No `bitriot_sim` source changed**: the build id is still `0x6767612d056ba8af` on GCC and Clang, the six golden digests and `tests/data/golden-round.brmr` are untouched, and `scripts/determinism-gate.sh` passes (17 variants matched on every tick, 1 not available as in 05b, the negative control caught at exactly tick 100 on all 17; it is configured with `-DBITRIOT_NETPLAY=OFF`). Local play is unchanged without netplay flags (all `bitriot_tests`, which include every simulation test, pass; the only edits to existing game code are listed below). The web build still configures (netplay OFF), builds, and `node tests/web/smoke.js` passes 41 of 41. What was *not* possible in the container is a person playing it: the two-real-windows run was driven under Xvfb with `xdotool`, which is evidence, not a play-test. The steps are below.

**What landed**

1. **vnet as a submodule** `ext/vnet`, pinned to the `v0.4.0` tag (eb1feeb). `-DBITRIOT_NETPLAY` (default ON on native builds that have the submodule, OFF for Emscripten and MinGW; asking for ON without the submodule, or on those targets, is a configure error that says what to do). `scripts/cloud-setup.sh` fetches the submodule when it is run from a checkout.
2. **`bitriot_net`** (`src/net/`, links `bitriot_sim` and vnet, no SDL):
   - `NetProtocol.*`: the thirteen messages (`HELLO`, `WELCOME`, `REJECT`, `LOBBY`, `START`, `INPUT`, `TICKS`, `CHECKSUM`, `DESYNC`, `PING`, `PONG`, `KEEPALIVE`, `LEAVE`), strict total decoders, an 8-byte header that never changes between versions, the protocol version first in `HELLO`.
   - `LockstepSession.*`: host and peer in one class (lobby, `START`, input delay, confirming and repeating ticks, acks, timeouts, leaving, desync detection). It knows TickInputs and MatchConfig but not SDL, not `Simulation`, not the hash algorithm.
   - `NetMatch.*`: a session driving a `Simulation`: start from the announced round, step with the confirmed inputs, hand the session the local sample, report a checkpoint hash every 30 ticks, record the **final** inputs in a `MatchRecorder`, keep a ring of the last 16 state dumps, and on a desync write the dump of the disagreeing tick, the dump of where it stopped, and the record.
3. **`bitriot_headless host|join`** (`src/headless/HeadlessNet.*`; also spelled `--host`/`--join`): no window, real UDP, scripted inputs (`--script`, a record file), `--takeover SLOT@TICK`, `--leave-at`, `--perturb-at` (the test-only desync provoker), `--delay`, `--tick-ms`, `--out`. stdout is identical on every machine (round header, a hash every 30th tick, control events, winner and final hash), so a test just compares it; exit codes follow 05 (0 played, 1 the session broke, 2 usage, 3 refused for another build or data).
4. **The game.** `--host [port]`, `--join host:port`, `--name`, `--local-players N`, `--players N` (`GameOptions`, `getopt_long`; `--host 7777` and `--host=7777` both work). `NetGame` (vnet context, session, saved settings, lobby text), `assets/menu/netlobby.rml`, and `GameEngine` hooks:
   - the session is pumped every frame in every state (menus, scoreboard, lobby), and the main loop sleeps on the socket (`vnet_wait`), so a datagram wakes the frame;
   - `FrameLoop::Hooks::canStep` and `extraTicks` are set (03b's wrap-don't-replace advice followed: `pumpEvents()` is wrapped, `renderFrame()` untouched except for the overlay);
   - `stepTick` becomes sample, `NetMatch::step`, rumble; `resetGame` becomes `resetNetGame` (the host's `MatchConfig`, every slot's keyboard and pad choice from the menu cleared, so this machine's players get keyboard sets 1, 2, ... in slot order and a pad's START binds it to one of this machine's own slots, then to a bot: see the second review pass);
   - **only the host's `START` begins a round.** The scoreboard's Play Again does nothing on a peer (it reads "Waiting for the host to play again..." and is greyed) and on the host waits until every peer holds the last tick; "Main Menu" leaves the session (`EventManager::SetCommandInterceptor`);
   - ESC (or closing the window) in a round **leaves**: a peer's slots become bots on every other machine on the same tick; a host that leaves ends the session, and the peers get a "Disconnected" page with the reason;
   - a machine that loses the host (10 s of silence) gets the same page; a desync writes the evidence and shows the file paths;
   - while a tick is held the HUD gets a red "waiting for <name>" box and the window title carries `NET host|peer D=<n> ping <ms>`;
   - settings isolation: `GameSettings::snapshot()` when netplay starts, `restore()` when it stops (leaving, quitting, the host going away).
   - `InputHandler::setPointers` takes the set of human slots this machine reads (default: all, so local play is unchanged); a human on another machine gets no keys, no pad and no local slot. `localSlotMask()` and `revokeSlot()` are new.
5. **`doco/netplay/protocol.md`** documents every message, the versioning rules, tick numbering, the merge, loss handling, D, timeouts, rounds and desync handling.

**Tests**

- `NetProtocolTest` (16) and `LockstepSessionTest` (51; the loss and desync cases are parameterised), in both executables, in virtual time on vnet's loopback. `bitriot_sim_tests` 412 to **479**, `bitriot_tests` 517 to **591** (the other 7 are `GameOptionsTest` (4), `InputControlTest` (2) and `FrameLoopTest` (1)), identical on GCC Release and Clang Release. The list of what they cover is in `tests/README.md` ("Netplay").
- `bitriot_netplay_process_tests` (6 cases, about a minute, its own ctest entry): see below.
- **Multi-process, over UDP on 127.0.0.1, ephemeral ports, `VNET_IMPAIR` with an own seed per process** (`lat=8,jit=3,loss=5,dup=1,reorder=2`): two processes play two rounds (the host starts the second) and print byte-identical output (every checkpoint hash, final hashes, winners); the two records are byte-identical and `bitriot_headless replay` accepts them; three processes with two local players on one machine, a bot taken over at tick 200 (`event 1 212 slot 4 HUMAN_TAKES_OVER`) and a peer that quits at tick 600 (both its slots `RETURN_TO_AI` in everyone's output); a desync provoked by `--perturb-at 355` is reported by all three processes at **tick 360, the first checkpoint after the perturbation**, with the same hashes and offender, and each writes the state dump of tick 360, the dump of where it stopped, and a parseable record; the unperturbed control does not desync; the host chooses D from a 120 ms round trip (5 to 8); and a host killed mid-round leaves the peer exiting 1 after about 10 s with "lost the connection to the host".
- **Proof the tests fail without the code.** 20 one-at-a-time mutations of `LockstepSession.cpp`, each rebuilt and run: all killed (list in tests/README). Four survived the first version of the tests (ownership in the merge, another round's ticks, an out-of-roster event from a peer, repeats between rounds); each got a test and was killed in a second pass. The negative control of D7 itself, `WithoutRedundancyLossStallsTheMatchForGood`: with `SessionOptions::redundancy = false` (every tick on the wire once) the same 5% loss that the normal case rides out freezes the match for good, and `WithoutRedundancyAndWithoutLossItStillWorks` shows it is the loss, not the switch.
- **Real game under Xvfb** (two `BitRiot` processes, `xdotool`; not committed): the lobby page on both (names, slots, 33 ms ping), Return on the host started the round on both with the same seed and delay 3, both windows showed the same picture with each player's keyboard badge on its own slot, ESC in the peer window returned it to the main menu while the host played on with that player a bot, ESC in the host window left the peer on the "Disconnected: the host ended the session" page, the scoreboard came up on both after the round with the peer's Play Again greyed out, the host's Return started round 2 on both, and the two `--record` files of round 1 were byte-identical and replay. With `VNET_IMPAIR` of 250 ms latency, 100 ms jitter and 30% loss in both processes the host chose D=8, the title read `NET peer D=8 ping 476 ms`, and both windows showed the red "waiting for P" box while the round was held. A `-b` host (25x19) made the peer's window resize to the host's map. Not checked in the container: controller and rumble paths, three or more real windows.

**Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, GCC Release, from the repository root, nothing else running): `bitriot_tests` (591) **0 failing seeds**, `bitriot_sim_tests` (479) **0**. No simulation source changed, so there is no "before" to compare (05's numbers stand: 0 and 0). A first sweep, run while mutation builds were going on in the same build tree, showed 3 and 2 failing seeds (13, 30, 31, 55); each passes alone and the clean re-run is 0 and 0. The review could not reproduce any failure under heavy load (an 80-seed sweep at 6-way parallel, those seeds three times beside a `-j4` build, the process tests beside 6 busy loops), so the likely cause is the mutation builds relinking `bitriot_tests`, `bitriot_sim_tests` and `bitriot_headless` under the running sweep. **Do not run a sweep in a build tree being rebuilt.**

**Decisions** (the brief was ambiguous or silent; the recommended option was taken)

1. **Submodule, not FetchContent.** The repo already vendors RmlUi as a submodule; a submodule pins the commit in git, needs no network at configure time (FetchContent with `GIT_REPOSITORY` would fetch at every fresh configure), and works offline. Cost: a fresh clone needs `--recurse-submodules`; `BITRIOT_NETPLAY` is a tri-state (AUTO by default, re-evaluated at every configure, so fetching the submodule later and re-running cmake turns it on); AUTO without the submodule prints a `WARNING` naming `git submodule update --init ext/vnet` and builds without netplay; ON without it is an error.
2. **`BITRIOT_NETPLAY` is OFF for Emscripten and MinGW** (brief 06 never verified vnet on MinGW) and the 05b gate passes `-DBITRIOT_NETPLAY=OFF` so it keeps testing exactly the simulation and `bitriot_headless replay`.
3. **Netplay lives outside `bitriot_sim`** (`bitriot_net`), so the build id does not move and `SimLibraryTest`'s guards are untouched.
4. **`host`/`join` are commands of `bitriot_headless`**, not `--host`/`--join` options, because 05's CLI is a command set; `--host`/`--join` are accepted as aliases. The game itself has the options.
5. **A record is made by stepping, so nobody can be more than D ticks ahead.** The brief asks the host not to run ahead of its slowest peer by more than D and to define the pacing. There is no separate rule: a machine produces its record for tick `t` only by stepping `t - D`, and the host confirms `t` only with every record, so the confirmed-but-unstepped backlog is at most D. `NobodyEverHoldsMoreThanDUnsteppedTicks` watches it through loss, a blackout and a paused peer.
6. **The catch-up rule is implemented as written and cannot fire in this design** (the backlog is at most D, the rule needs D + 3). It is a safety net for a varying D or hot-join (13); its rule is tested as a function. After a stall nobody needs to run faster: everyone resumes at the nominal rate with D ticks in flight.
7. **Events travel with the tick they belong to.** A peer's `HUMAN_TAKES_OVER` rides in the record of the tick it was sampled for (+ D), together with that tick's inputs, as 04b's follow-up asked; the host validates each event (known kind, slot inside the roster, sender owns the slot, or the slot is a bot's) before it enters a tick, and drops the rest without a trace. Two machines claiming the same bot in the same tick: the lower peer id wins, and the loser is told (`takeRejectedClaims()`; the game calls `InputHandler::revokeSlot`). A leaving peer's slots are `RETURN_TO_AI` events, as the brief says (13 may switch to `LEAVE`).
8. **A hash is reported every 30 ticks and on the round's last tick** (the brief's interval), the host compares each peer with its own, and a mismatch stops everybody (`canStep()` false), with 1.5 s of lingering so the DESYNC reaches the others. The ring keeps 16 dumps (about 16 seconds, many times what a DESYNC takes).
9. **D is chosen once per round** from the host's lobby/scoreboard pings, with the brief's formula: `ceil((worst RTT + jitter) / 33 ms) + 1`, clamped 2 to 8, where worst is the largest of the last 16 samples and jitter their mean absolute deviation; D = 3 when nothing has been measured. The host logs how it got the number. Note that in the game the pings themselves are answered at frame rate (30 Hz in menus), so the measured RTT includes up to two frames; that is what the in-game pipeline sees too.
10. **A START is repeated until the peer's first INPUT arrives, and a next round cannot start until every peer holds the last tick of the last one** (`canStartRound()`), otherwise a slow peer would wait for ticks the host stopped sending. Every packet carries its round number; a packet of another round is thrown away.
11. **The running-round keepalive is an empty INPUT / TICKS**, so it also carries the ack and (host to peer) the "who am I waiting on" mask; `KEEPALIVE` proper is for the lobby and the scoreboard.
12. **The lobby is a minimal RmlUi page**, not text-mode: names, slot numbers, ping, map, D, and a Start button on the host; settings (map, blocks, spawner) are the host's menu values (and `-b`), `--players N` fills the rest with bots, and the map is raised automatically to the menu's minimum for five or six players. Editing settings in the lobby is 08.
13. **A peer that quits with the window close or SIGTERM mid-round leaves the round**, the same as ESC (they are one flag in the input layer, as in local play), rather than closing the application. A peer that wants to leave the application uses the scoreboard's Main Menu then Exit.
14. **The session has its own tiny generator** (`NetRandom.h`, splitmix64 seeded from `/dev/urandom` and the clocks) because `SimRandomSourceGuardTest` forbids `<random>` anywhere in `src/`. It makes session ids, nonces and tokens (not secrets: the session id travels in the clear).
15. **`vnet_wait` in the main loop**, so a datagram ends the frame sleep; `FrameLoop::setHeldPollMs` (default 2 ms, unchanged) lets a network game poll a held tick every 4 ms instead of spinning at 500 Hz (03b's follow-up (a)).
16. **The stall overlay is the window title plus a small box drawn with SDL_ttf** (`assets/fonts/FreeMono.ttf`), because `InfoPanel::drawTextGDI` is a no-op. Real HUD text is 08's.
17. **The headless runner uses IPv4 only and `vnet_wait(2)`**; the game uses vnet's dual-stack default. Both honour `VNET_IMPAIR`, so the real game can be tried under simulated latency.

**Deviations from the brief, and brief/code-facts corrections**

- The brief's "Add a `BITRIOT_NETPLAY` CMake option: ON by default" is ON only when the submodule is present (decision 1).
- The brief says the host "doesn't run ahead of its slowest peer by more than D. Define exactly how it paces": decision 5.
- `doco/netplay/research/code-facts.md` §8 ("there is no networking code") is rewritten; §1 and §12 note the new hooks, the `setPointers` argument, the scoreboard interceptor and the new options. The brief's `--host`/`--join` for `bitriot_headless` became commands (decision 4).
- `HELLO` carries the name and local-player count as the brief says; `WELCOME` carries the slots and a reconnect token that nothing uses yet (13).

**Follow-ups**

- **08 (lobby UI).** The lobby is a text page: no editing of map, blocks, spawner, teams or slot assignment, no ready flags, no kick, no chat. `LobbySettings` and `LobbyView` are the seams: the host's `setLobbySettings()`, and per slot owner and team in `LOBBY`. A real HUD overlay for "waiting for <name>" and a ping display belong there too; `NetGame::statusLine()`/`stallText()` have the text. The scoreboard's Play Again is host-only now.
- **12 (browser netplay).** The session is transport-agnostic (it only needs `vnet_sendto`/`recvfrom`, `vnet_time`, and peer addresses; `LockstepSession::join` takes a `vnet_addr`), but: D is clamped to 2..8, so a relay's two hops may want a wider clamp (the `proposedDelay()` formula and `MAX_DELAY` are one place); a hidden tab that pumps once a second will hit the 10 s timeout on its peers, so the session needs a "pumps are sparse, do not time anyone out on a gap" rule like vnet's own (not needed natively); `NetGame` creates a UDP context, so the browser needs a relay context (`VNET_BACKEND_RELAY_AUTO`, and 14's `vnet_config.wake`); `std::filesystem` and `/dev/urandom` in `NetRandom.h` need a browser alternative (the seed option exists).
- **13 (disconnects, hot-join).** `WELCOME` issues a reconnect token and `HELLO` has the field; nothing uses them. A mid-round joiner is refused (`REJECT_IN_PROGRESS`); the world for it is `MatchRecorder::record()` serialised at the moment of the join, as 05's follow-up says, and `NetMatch` already holds the recorder of final inputs. A leaving peer is `RETURN_TO_AI` today. The `Member::gone` state (slots turned to bots, address remembered so a late packet is told it was kicked) is where a reconnect would hook in.
- **Known limits, not fixed.** No authentication or encryption (a session id is a random 32 bits in the clear); no NAT traversal (vnet has none: one side needs a forwarded UDP port, or a relay); a debugger pause of more than 10 s drops the machine; the loser of a takeover race is told only D ticks later, so for that time its keyboard drives nothing; a peer that quits by closing the window mid-round leaves the round, not the application.
- **Untested glue** (needs a display): the overlay drawing, `showNetPage`'s text, `revokeSlot` through the real handler (its effect is tested), and everything with a controller (pad join, unplug and rumble in a networked round use the same 04b paths with the local mask; not run).
- A frame loop that sleeps at most one tick pumps the network at about 30 Hz in menus, which is where the measured 33 ms ping comes from; On localhost the measured ping is one or two frames (about 0 to 33 ms, shorter now that `vnet_wait` wakes the loop on a datagram), so D is 2 or 3 there; a dedicated network thread would lower it further; not worth it before a play-test.

**Play-test** (pending; needs a display, two windows are enough, a second machine or a pad makes it better). The commands are untested on Arch (the container is Ubuntu); the dependencies are the same as the earlier briefs' (RmlUi 4.3 built from source with the two flags Arch's newer tools need).

```bash
# one-time: dependencies, if you do not have them from the earlier briefs
sudo pacman -S --needed base-devel cmake git sdl2 sdl2_mixer sdl2_ttf sdl2_image freetype2 mesa
git clone --depth 1 --branch 4.3 https://github.com/mikke89/RmlUi.git /tmp/RmlUi
cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_SAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_LUA_BINDINGS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"
cmake --build /tmp/RmlUi/build -j"$(nproc)" && sudo cmake --install /tmp/RmlUi/build && sudo ldconfig

# the `claude` branch (07 is merged). --recurse-submodules fetches ext/vnet.
git clone --recurse-submodules --branch claude https://github.com/VenKamikaze/BitRiot.git ~/bitriot-net
git -C ~/bitriot-net submodule update --init ext/vnet    # a no-op after --recurse-submodules; needed in an existing checkout
cmake -S ~/bitriot-net -B ~/bitriot-net/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-net/build -j"$(nproc)" --target BitRiot
cd ~/bitriot-net/build
```

**Two windows on one machine** (two terminals, both in `~/bitriot-net/build`):

```bash
./BitRiot --host 7777 --players 4 --name Alice --record /tmp/alice          # terminal 1
./BitRiot --join 127.0.0.1:7777 --name Bob --record /tmp/bob                # terminal 2
```

(The windows open on top of each other: drag one aside. Alice's keyboard is WASD plus Left Ctrl / Left Shift; Bob's, in his own window, is the same keys, because each machine gives its own player the first keyboard set. Click a window before typing in it.)

1. **Lobby.** Both windows show "Network game", the map, the player list with slot numbers and Bob's ping (about 30 ms on one machine). The host has a "Start match" button; Bob's says "Waiting for the host to start the match". The window titles say `NET host` / `NET peer`.
2. **A full match.** Press Enter in Alice's window (the button is focused). Both windows start the same round: Alice is player 1, Bob player 2, two bots. Play it out. Look at: both pictures staying identical (the bots, the monsters and every explosion happen on the same tick in both), your own input feeling immediate (`D=3` is 100 ms of delay; the title shows `D=` and Bob's ping), and no "stall" box appearing on a quiet LAN. With `VNET_IMPAIR="lat=80,jit=20,loss=5,seed=1"` set in Bob's terminal before starting (and a different seed in Alice's) the host picks a bigger D, input gets laggier, and a red "waiting for ..." box may flash when a packet is late.
3. **The scoreboard and a second round.** When the round ends both show the scoreboard. Bob's Play Again reads "Waiting for the host to play again..." and pressing it does nothing. Alice's Play Again starts round 2 on both (the logs print `Network round 2, seed=..., delay=...`).
4. **A peer leaves, and its player becomes AI.** Start another round, then press ESC in Bob's window: Bob goes to the main menu, and in Alice's window player 2 carries on **as a bot** (it walks and fights on its own from a tick or so later). The host's round is not interrupted. Bob can start the game again locally: his settings (players, map) are the ones he had before, not the host's.
5. **The host leaves.** Join again (round 1 of a fresh `--host`), then press ESC in Alice's window: Alice returns to the main menu and Bob's window shows "Disconnected / the host ended the session" with a Leave button that returns him to the main menu. Also try killing the host's process (`kill -9`) in a round: after about 10 seconds Bob gets "lost the connection to the host".
5b. **A pad as your own player.** With a pad connected, press its START once in a round: it takes over your own slot (the keyboard set is released); pressing START on a further pad takes your next own slot, and after that a bot (the selector).
6. **A takeover of a bot.** With `--players 4` there are two bots. In Bob's window press the action-1 key of an unused keyboard set (arrows: Right Ctrl; keypad: KP+) to open the selector, up/down to choose, action-1 to take the bot over. After a moment (D ticks) it is yours in Bob's window and Alice sees the same bot start to move with Bob's input.
7. **Records.** After a round `ls /tmp/alice /tmp/bob`: both hold `round-1-peer<N>.brmr`; `cmp` says they are identical and `./bitriot_headless replay /tmp/alice/round-1-peer0.brmr` (from the build directory) says `replay: OK`. If a desync is ever reported the page lists the files written (`desync-r<round>-t<tick>-p<peer>-state.txt`, `...-now-...`, and a `.brmr`); keep them and diff the two machines' `-state.txt`.
8. **A real second machine** (optional): on the host `./BitRiot --host 7777 --players 4`, on the other `./BitRiot --join <host ip>:7777`; the host needs UDP port 7777 reachable.
9. **Closing the window or SIGTERM mid-round** leaves the round (like ESC) but keeps the process: a peer ends on the main menu, it does not quit. Use Exit on the menu to quit.
10. **Regression:** `./BitRiot` with no flags is the game as before: menu, a local round, scoreboard, ESC, Play Again, map-size change.

Record the result here when it comes back.

**Final numbers at merge** (coordinator, after the second review pass, GCC Release, nothing else running): `bitriot_tests` **599**, `bitriot_sim_tests` **485**, ctest 3/3 (the process test has 7 cases); the same 599 and 485 on Clang Release (implementer). 200-seed sweep: 0 failing seeds on both binaries. `scripts/determinism-gate.sh`: 17 variants matched, 1 not available, the negative control caught at tick 100 on all 17. The web build builds and `tests/web/smoke.js` passes 41 of 41. `bitriot_headless version`: build id `0x6767612d056ba8af` (unchanged).

**Review fix round** (one Opus review, "merge after fixes")

- **S1.** A desync in a round's last ticks was never detected: the final-tick hash is reported as the round finishes, the peer was `BETWEEN_ROUNDS` before its next pump, and `sendChecksums` returned unless `RUNNING` (also for the 400 ms repeat). It now sends between rounds too, the host's `canStartRound()` refuses while a desync is known or while a peer that played the round has not yet reported the hash of the round's last checkpoint, and the headless peer exits 1 when the host closes after telling it of a desync. Tests: `ADesyncInTheLastTicksOfARoundIsStillDetectedAndHoldsTheNextRound` (clean and 20% loss), `TheNextRoundWaitsForTheLastTicksHashToBeCompared`, and the process case `ADesyncInTheLastTicksOfARoundIsDetectedToo` (seed 10, perturbed at 1515, round over at 1526). Without the fix (sends only while running) both the session test and the process test fail, along with seven other between-rounds tests. Removing only the `canStartRound` hash condition is *not* caught: the acks already hold the next round back long enough in every test.
- **S2.** One event per slot per machine per tick, and at most 18 peer events in a merged tick (the host's leaver events, at most 6, come after), so a tick never exceeds the protocol's 24; a packet that does not decode no longer counts as a sign of life. Tests `AHostilePeerCannotFreezeTheOthersWithATickTooFullOfEvents` (fails without the cap: the other peer freezes) and `GarbageIsNotASignOfLife` (fails without the change).
- **S3.** `BITRIOT_NETPLAY` is AUTO/ON/OFF (above); the README and the play-test steps say `git submodule update --init ext/vnet`.
- **N1.** The host remembers who owned each slot after each confirmed tick and a TICKS datagram carries the owners after the last tick it actually holds (the round's own, not the next round's re-deal). Test `ATakeoverInTheLastTicksOfARoundIsNotRevokedByTheNextRoundsSlots` (passes before and after in these seeds; it is a regression guard).
- **N2.** `Reader::str` no longer mangles bytes from 0x80 up (UTF-8 names survive); test added.
- **N3.** `NetMatch::step` catches and logs a recorder refusal (past `MAX_TICKS`) and stops recording, like the local path.
- **N4.** A networked round clears every slot's menu keyboard and pad; this machine's players get keyboard sets in slot order, and a pad joins a bot with START.
- **N5.** HELLO now checks version, build and data before anything else, then the repeat or restart of a known address (documented in protocol.md); `LockstepSession::flushOutgoing()` keeps polling up to 250 ms after `leave()` in the game and in `bitriot_headless`, so LEAVEs held by simulated latency go out; the D and SIGTERM text above is corrected.

**Second review pass**

- **R1 (a pad could not drive this machine's own slot).** Design: in a network game a pad's START **first binds the pad to one of this machine's own human slots that has no pad** (lowest first; that slot's keyboard set is released), and only when none is left opens the join selector for a bot (`InputHandler::setPadsMayBindOwnSlots`, set by `resetNetGame`, cleared by local `resetGame`, so local play is unchanged). It is local only: no control event, nothing on the wire or in the record; a takeover of a bot remains the only control event. A pad-only player therefore presses START once at the start of the round (keyboard players need nothing). Chosen over dealing pads at round start because the game has no list of plugged-in pads and START is also how a pad is introduced mid-round everywhere else. Tests: `InputControlTest` (two pads bind our two slots, the third takes a bot with exactly one `HUMAN_TAKES_OVER`, the pad's buttons reach the sampler; without the flag START only opens the selector).
- **R2.** The last event per slot in a record is kept (take then give back means given back); the earlier are dropped. Test `TakeThenGiveBackInOneTickMeansGivenBack`.
- **R4.** `flushOutgoing()` returns at once unless vnet reports datagrams accepted but not yet on the wire (held by simulated latency), and waits at most 250 ms for them.
- **R5.** `BITRIOT_NETPLAY` is upper-cased and CMake's true/false spellings are accepted; anything else is a FATAL_ERROR.
- **R3 (follow-up for 13).** `canStartRound()` waits for every peer's final-tick hash with no time limit and no message. It is not reachable today (a peer that ended on another tick would have desynced at an earlier checkpoint), but 13 should bound the wait, then raise a desync or drop the peer.
