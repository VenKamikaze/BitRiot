# 08 — Lobby UI

- **Repo:** BitRiot
- **Branch:** `netplay/08-lobby-ui`
- **Depends on:** 07
- **Status:** done (play-test pending)

## Goal

Let players host and join online games from the menus, without command-line flags. They should be able to:

- see who is in each slot;
- set teams and settings (host only);
- see network state during play: waiting for a player, a desync, a player leaving.

## Where 07 left things (read this first)

Brief 07 (merged in 0ccdb66) built native lockstep netplay, driven from the command line. The game runs it with `--host [port]`, `--join host:port`, `--name`, `--local-players N` and `--players N`. This brief puts a real UI on top of it. **Read 07's Outcome in full**, especially:

- decisions 9–13: D, START and round gating, the running keepalive, the lobby page, and leaving;
- "Second review pass" (R1: a pad's START binds this machine's own slot first, then a bot);
- the follow-ups for 08.

**What exists today, and what it gives you:**

- **`src/net/LockstepSession.h`** (no SDL, no `Simulation`):
  - **Host:** `setLobbySettings(LobbySettings)`. That holds map width and height, block percentage, NPC spawner, total slots, and a team per slot. `canStartRound()` and `startRound()` go with it.
  - **Both roles:** `lobby()` returns a `LobbyView`: the phase (lobby, running, between rounds), the round number, the settings, the per-slot owner and team, and each peer's id, name, slot mask, RTT and connection state.
  - **Peers:** `rejectInfo()` (reason, the host's protocol, build id and data hash, plus a sentence), `endReason()` and `endText()`, and the `State`.
- **`src/NetGame.{h,cpp}`** joins the session to the game:
  - `NetGame::Params`: host/port/join/name/localPlayers/totalSlots/recordDir;
  - `create()` resolves a host name (it blocks, so call it before the frame loop or on a worker);
  - `startRound()` takes map, blocks and spawner from the menu settings, and raises the map for five or six players;
  - `restoreSettings()`;
  - text helpers: `lobbyTitle()`, `lobbyText()`, `statusLine()`, `stallText()`.
- **`GameEngine` net hooks** (`src/GameEngine.cpp`, about lines 540–740): `startNetplay`, `stopNetplay`, `leaveNetplayToMenu`, `showNetPage`, `netFrame`, `resetNetGame`.
  - `EventManager::SetCommandInterceptor` makes "start", "open mainmenu.rml" and "exit" behave on the scoreboard.
  - The stall box is drawn with SDL_ttf over the game, because `InfoPanel::drawTextGDI` is a no-op.
  - The window title carries `NET host|peer D=<n> ping <ms>`.
- **`assets/menu/netlobby.rml`/`.rcss`** is a *text* page:
  - a title, a `netlobbytext` div filled from `lobbyText()`, and Start/Leave buttons;
  - the "Disconnected" and "Desync" pages reuse it.
  - **This is what 08 replaces.**
- **Reject reasons** (`NetProtocol.h`, `RejectReason`): `REJECT_VERSION`, `REJECT_BUILD`, `REJECT_DATA`, `REJECT_FULL`, `REJECT_IN_PROGRESS`, `REJECT_BAD_REQUEST`. Each carries the host's numbers and a sentence. 08 must show them clearly.

**What the protocol does not have yet.** Adding these means changing the wire format:

- `LOBBY` carries per slot only `owner` and `team`. There are **no ready flags**, no message for a peer to say it is ready, and no way for the host to move a peer to another slot or remove it.
- The protocol is version 1 ([`protocol.md`](protocol.md), "Versioning"). Any layout or meaning change after the 8-byte header **bumps `PROTOCOL_VERSION`**: add a message such as `READY` (peer to host), and extend `LOBBY`. Then update `protocol.md`, the strict decoders and their tests (`NetProtocolTest`), and keep `LockstepSession` free of SDL.
- A **"back to lobby"** step between rounds (so the host can change settings or let someone leave) is not in 07. Today the session goes `BETWEEN_ROUNDS` → next `START`. Decide whether 08 adds a phase change from between rounds back to the lobby, or whether the host simply changes settings between rounds (`setLobbySettings` already applies to the next `START`). Document the choice.

**Must keep working:** the command-line flags, which the play-test steps and `bitriot_headless host|join` use. The multi-process ctest `bitriot_netplay_process_tests` runs through `bitriot_headless`, not the menus, and must stay green.

## Read first

- Brief 07 and its Outcome (above), [`protocol.md`](protocol.md), and `src/net/LockstepSession.h`, `src/NetGame.h`, `src/GameEngine.cpp` (the net hooks).
- `assets/menu/*.rml` and `*.rcss`, in particular `mainmenu`, `playerselect`, `gameoptions`, `scoreboard` and `netlobby`.
- `src/RmlUI/` binders and event handlers (`PlayersSelectBinder`, `EventHandlerPlayerSelect`, …) and `src/MenuSDLRenderer.cpp`.
- `tests/support/RmlLayoutHarness.*` and `tests/ScoreboardLayoutTest.cpp`, which show how menu layouts are asserted headlessly. `tests/support/SessionHarness.h` (07) runs N sessions on vnet loopback in virtual time.
- `tests/README.md` ("Netplay"), and code-facts §1, §7, §8 and §12 (07 rewrote §8).
- Today's local flow: main menu → player select → game options → start.

## In scope

1. **Main menu:** add "Host online game" and "Join online game" alongside local play.
   - Only show them when the build has netplay (`BITRIOT_NETPLAY`; it is OFF for the web build until brief 12 and on MinGW). The web smoke test (`tests/web/smoke.js`) must still pass.
2. **Host screen:**
   - The listen port, defaulting to 7777, with a note that UDP port N must be reachable (forwarded) for players outside the LAN.
   - Then an **online lobby** based on the player-select screen. Each slot shows one of:
     - host-local;
     - a remote peer's name, with its local index;
     - AI;
     - open.
   - Each slot also shows its team, ready state and ping.
   - The host edits map, blocks, spawner, total slots and teams with the existing game-options controls (through `setLobbySettings`).
   - The host starts once everyone is ready. Start stays disabled while `canStartRound()` is false or someone isn't ready.
3. **Join screen:**
   - An address field (`host:port`; host names are allowed, and `NetGame::create` resolves them) and a name field. Keyboard text entry is enough; controller-friendly entry is a nice-to-have.
   - The number of local players to bring (1 to the number of keyboard sets; pads bind with START in the round, per 07 R1).
   - Then the same lobby, read-only except for this peer's own ready state.
   - **Clear errors**, built from `rejectInfo()` and `endReason()`:
     - a different game version, i.e. build id or protocol (say whose is newer if that is knowable, and show both build ids);
     - different game data;
     - lobby full;
     - a match already in progress;
     - host not answering;
     - host left.
   - A connection attempt must not freeze the menu: the host-name lookup blocks.
4. **In-game overlays.** Replace 07's SDL_ttf stall box and title-bar status with proper HUD or RmlUi overlays:
   - "Waiting for <name>…" after about 0.5 s of stalling (`stallText()` / the TICKS waiting mask);
   - "<name> left — now AI";
   - "Desync at tick N — dumps written to <path>" (07 writes them; `NetMatch` knows the paths).
   - Ping per peer in the InfoPanel is optional.
5. **Between rounds:**
   - The scoreboard shows "Waiting for host…" on peers. 07 already greys out Play Again and changes its text there.
   - The host gets "Next round", and can return everyone to the lobby or change settings (see "back to lobby" above).
6. **Settings isolation.** 07 snapshots the local `GameSettings` when netplay starts (`GameSettings::snapshot()`) and restores them when it stops. Make sure every menu path into and out of an online game goes through that, so a joiner's local-play settings survive.
7. **Persist the last used name, address and port** in a small settings file under `SDL_GetPrefPath`.
   - Nothing else is written there today (only `--record` and desync dumps write files).
   - Keep the format trivial, e.g. `key=value` lines, and tolerate a missing or corrupt file.
   - Native only: the web build has no persistent filesystem until brief 12 adds IDBFS, so it must compile and simply not persist there.

## Out of scope

- Relay and room codes. Brief 12 adds "Join by room code".
- Matchmaking, server browsers and chat.
- Hot-join and reconnect (brief 13). A mid-round join stays `REJECT_IN_PROGRESS`.
- Kicking a peer is optional. If it is cheap once the protocol grows a host-to-peer "you were removed" path, fine; otherwise list it as a follow-up.
- Changes to `bitriot_sim`. The build id `0x6767612d056ba8af` and the golden digests must not move. If one does, run `scripts/determinism-gate.sh` and say why in the commit.

## Tests

- **Layout tests** for the new screens, using `RmlLayoutHarness`: the lobby slot table lines up for 2–6 slots, and long and UTF-8 names (07 keeps bytes from 0x80 up) are truncated or wrapped without overflowing.
- **Binder tests:** a `LobbyView` with mixed slot kinds (host-local, remote, AI, open) and ready states maps to the expected rows and flags.
- **Protocol and session tests** for anything added to the wire: encode/decode round trips, strict rejection of malformed input in `NetProtocolTest`, and ready and start gating in `LockstepSessionTest` with `SessionHarness`. Also a version-1 `HELLO` must be refused with `REJECT_VERSION` once the version is bumped.
- **Flow tests:** drive the menu state machine without SDL where feasible. Otherwise document a manual test script for the user, with exact steps for two windows on one machine.
- **Don't break 07's tests or the web smoke test.** Run the full suites, ctest (including `bitriot_netplay_process_tests`), a NETPLAY=OFF build, and the web build with `node tests/web/smoke.js build-wasm`.
- **Never run a seed sweep in a build tree being rebuilt.**

## Acceptance criteria

- The host/join flow works end to end from the menus. The user play-tests it with two windows on one machine, following the documented steps.
- Local play, the command-line netplay flags and the web build are unchanged.
- New screens have layout tests, and wire changes have protocol and session tests. `protocol.md` is updated, with the version bumped.
- Errors are visible and specific.

## Outcome

**Status: done, play-test pending.** The host and join flow works from the menus end to end: a main-menu entry for each, a host form (name, UDP port, players here), a join form (name, address, players here) with the host-name lookup off the frame loop, an online lobby (six seats with host-local / remote / computer / open rows, team, ready and ping; the host edits map, blocks, enemies, player count and teams and can remove a peer; a peer toggles ready), specific error pages for every refusal and ending, an RmlUi overlay in the round (waiting for a player, who left, a desync, a status line), the scoreboard's "Next round" / "Back to lobby" for the host and "Waiting for the host..." for peers, and the last name, address and port remembered. **No `bitriot_sim` source changed**: the build id is still `0x6767612d056ba8af` (`bitriot_headless version`, GCC and Clang), the six golden digests and `tests/data/golden-round.brmr` are untouched. The command-line flags, `bitriot_headless host|join`, local play and the web build behave as before (the multi-process ctest passes, the web smoke test passes, the `-DBITRIOT_NETPLAY=OFF` build is the game it was). What was *not* possible in the container is a person playing it: the real game was driven under Xvfb with `xdotool` (two windows, screenshots), which is evidence, not a play-test. The steps are below.

**What landed**

1. **Protocol version 2** (`doco/netplay/protocol.md` updated; `PROTOCOL_VERSION` 2): a `READY` message (peer to host: round, a `seq`, ready; a state repeated until the host's `LOBBY` shows it, newest `seq` wins, only for the next round's number and only in the lobby), a ready byte per peer in `LOBBY`, and `LEAVE` reason 4 (the host removed you). The strict decoders refuse a ready byte that is not 0 or 1 and a `LEAVE` reason above 4 (it used to take any). A version-1 `HELLO` is answered with `REJECT_VERSION` naming protocol 2, like any other version.
2. **`LockstepSession`** (still no SDL): `SessionOptions::requireReady` (off by default, so the headless runner, 07's tests and the multi-process test are unchanged); `setReady()` / `wantsReady()` / `notReadyCount()`; host `kick(peerId)` (lobby only), `returnToLobby()` (between rounds, under the same conditions as the next round) and `cannotStartBecause()`; `END_REMOVED`; a team chosen when there were more slots than there are now falls back to the slot's own number (it made `MatchConfig::problem()` refuse the round).
3. **`src/net/LobbyModel.*`** (new, no SDL, no RmlUi, in `bitriot_net`): everything the pages say as plain data: the six-row table (`buildLobbyRows`/`buildLobbyScreen`), the sentences ("Waiting for Bob and Carol to be ready."), `describeEnd` (why a join or a session ended: both build ids, which protocol is newer, the address typed, the 10 s timeout, host left, removed ...), `parseHostPort`/`parsePort`, the `key=value` settings file, UTF-8 `sanitizeUtf8`/`clipUtf8`/`clipUtf8Bytes`, the toast for a leaving player and the 0.5 s `StallTimer`.
4. **`NetGame`**: `NetResolve` (a host:port lookup on a detached worker thread, polled once a frame; `vnet_resolve_host` blocks), `createResolved`, the host's lobby controls (`cycleSetting`, `cycleTeam`, `kick`, `returnToLobby`), `lobbyScreen()`/`endMessage()`; **`pump()` gives the player's own settings back the moment the session has ended**.
5. **The pages** (`assets/menu/`): `nethost.rml`, `netjoin.rml` (+ `netform.rcss`), a rewritten `netlobby.rml/.rcss` (the same document is the lobby, the "Connecting..." page and the error page), `netoverlay.rml/.rcss`; the scoreboard gained a hidden "Back to lobby" (host only). `src/RmlUI/binders/NetLobbyBinder.*` pastes a `LobbyScreen` into the lobby document (six fixed rows: a ping changing is a text change, so a focused button keeps its focus); `EventHandlerNetLobby`/`EventHandlerNetForm` turn button presses into `NetUiRequests`.
6. **Menus**: the main menu's online entries come from an `EventManager::SetDocumentLoadedHook` (so a build without netplay shows the menu it always had); text input (`SDL_TEXTINPUT` into RmlUi, SDL's text input on only while a text field has the focus; Enter goes to the next field); Up/Down and the D-pad skip tab indexes that are hidden (`MickRmlUIElementUtil::stepTabIndex`); `MenuSDLRenderer` gained `setNetOverlay`/`renderNetOverlay` (the overlay is drawn by RmlUi right after the game picture is pushed; 07's SDL_ttf stall box and the window-title status are gone), `setScoreboardNet`, `showPage`, `setFormError`.
7. **`GameEngine`**: `netIdleFrame()` (host form -> `startNetplay`; join form -> `NetResolve` -> `NetGame::createResolved`; a failure before there is a session shows its own error page), `netFrame()` extended (Start or Ready, settings, teams, remove, the scoreboard's two host buttons, the error page and where its button goes, the overlay).

**Tests** (GCC Release; the same on Clang Release)

- `bitriot_tests` 599 -> **674**, `bitriot_sim_tests` 485 -> **530**; ctest 3/3 (the process test still has its 7 cases). `-DBITRIOT_NETPLAY=OFF`: `bitriot_tests` 526 and `bitriot_sim_tests` 412 (all of the pre-netplay tests, as at 07's merge).
- New: `NetProtocolTest` (+2), `LockstepSessionTest` (`LockstepLobby.*` 18, `AVersionOneHelloIsRefusedWithRejectVersion`; includes a hand-written raw-socket peer for `READY`), `LobbyModelTest` (24, both executables), `LobbyLayoutTest` (12 layout/binder tests + 7 `NetFormTest`) and `NetGameTest` (3 `NetResolve` + 8 over real UDP on 127.0.0.1), in `bitriot_tests`. What each proves is in `tests/README.md` ("The online lobby").
- **Proof they fail without the change** (mutations applied one at a time, the executable rebuilt, the scratch script not committed; the full list is in `tests/README.md`): about 70 mutations, all killed except one equivalent (clearing ready flags again in `returnToLobby()`: nothing observable, so the code was removed) and two CSS ones that change nothing a layout test can see (a fixed row height; `overflow: visible` on cells that the model already clips). Three survived the first version of the tests and were fixed: `READY` sent once instead of repeated (the loss test now runs 8 networks that each lose 60%), a bad port in the saved file (a file whose only port is bad), and the lookup run inline (the test now has a lookup that blocks 3 s and checks the constructor returns at once). Examples: `NetGame::pump` not restoring the settings fails `RemovingAPeerEndsItsSession...` and `WhenTheHostLeaves...`; the lookup run inline fails `ALookupThatBlocksDoesNotBlockTheCaller` (3 s); `PROTOCOL_VERSION` back to 1 fails `AVersionOneHelloIsRefusedWithRejectVersion` and `TheVersionIsTwoAnd...`; the name clip removed fails `LongAndUtf8NamesDoNotOverflowTheirCells`, `LobbyModelRows.LongAndOdd...` and two more.
- **Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, GCC Release, from the build directory, nothing else running): `bitriot_tests` (674) **0 failing seeds**, `bitriot_sim_tests` (530) **0**. No simulation source changed, so there is no "before" to compare (07's 0 and 0 stand). The sweep ran four seeds at a time in parallel (four workers on the four cores) on a tree that was not being rebuilt; the first few seeds ran while a stray game window was spinning a core, which only made it a harder test of the real-time `NetGameTest` cases.
- **Web**: `node tests/web/smoke.js build-wasm` passes **41 of 41** (the web build has netplay OFF; the main menu there has the three entries it always had).
- **Real game under Xvfb** (two `BitRiot` processes, `xdotool`, screenshots; not committed; evidence, not a play-test): the main menu with the two new entries; the host form (typing, the saved port); the lobby on both machines (names, rows, ping 5 ms, host-only team/setting/remove buttons); the peer pressing Ready and the host's status and the row changing; the host's width/height/team changes arriving in the peer's lobby; Start, a round on both with the status line at the bottom; a stalled peer (`SIGSTOP`) giving the host the red "Waiting for Bob..." after half a second; ESC in the peer giving the host "Bob left - now AI"; the scoreboard on both (host: Next round / Back to lobby / Main Menu; peer: "Waiting for the host..." greyed); "Back to lobby" returning both to the lobby with ready cleared; removing a peer ("You were removed", Back to the join form); a join to a port nobody answers on ("The host did not answer" after 10 s, with the address typed); the address check ("Put an IPv6 address in square brackets..."); the host leaving ("The host left"); the saved name and address pre-filling the join form; a join from the menus into a running lobby; the joiner's own *Game Options* (map width 19) still there after joining, seeing the host's map and leaving. Not checked (as glue, under Xvfb): pressing *Next round* (the unchanged 07 path with a new label), a desync's overlay box, the build-id and data-hash pages with really differing builds (their words and size are tested), a real controller, more than two windows, a second machine. 

**Decisions** (the brief left these open; the recommended option was taken)

1. **"Back to lobby" is a scoreboard button, and settings are edited in the lobby only.** Alternative: let the host change settings between rounds (`setLobbySettings` already applies to the next `START`). Rejected: the scoreboard has no room for a table of settings, a peer could not see what changed, and nobody could leave or join cleanly. Cost: one more wire state (the host's `LOBBY` phase 0 after phase 2 with the next round's number, and `returnToLobby()`); peers follow when they have finished their own round.
2. **Ready is asked for in the lobby only.** Between rounds nobody has to be ready again (the roster is the one that played), and a machine that joined between rounds cannot say ready until the host is back in the lobby. Ready flags are cleared when a round starts. A change of settings does **not** clear them (the lobby shows the settings live; a "settings changed, are you still ready?" would need a settings epoch on the wire).
3. **The command-line host does not require ready** (`NetGame::Params::requireReady` is true only for a host opened from the menus), so 07's play-test steps and `--host` behave exactly as before; the ready toggle still works there and the host's status says who has not answered.
4. **Kick is in** (cheap once `LEAVE` had a spare reason): lobby only, one button per remote machine, no ban list (the machine may join again). **Moving a peer to another slot is not** (slots are dealt in machine order; a follow-up).
5. **"Open" seats** are the rows beyond the match's player count (the table always has six rows, so it keeps its shape); a computer seat is a seat a person takes by joining (humans are dealt first) or, in a round, by taking the bot over. There is no separate "reserved empty" seat kind.
6. **Teams**: the host cycles each seat's team; a team beyond the seat count falls back to the seat's own number (in the session, so every caller gets it).
7. **Overlay**: RmlUi, drawn over the pushed picture (not composited into the CPU surface), so it needs no redraw bookkeeping. The 07 window-title status was removed. The "left" toast knows names for seats that were someone's at the round's start; a takeover mid-round has no name on every machine (the owner is not in the tick), so a later leave reads "A player left - now AI".
8. **A join never freezes the menu**: the lookup is a detached worker thread (`NetResolve`), polled once a frame; "Connecting..." shows a Cancel button. A failed lookup, a refusal and a timeout all end on an error page whose button returns a joiner to the join form (the address is kept) and a host to the main menu.
9. **Saved settings**: `name=`, `join=`, `port=`, `players=` in `netplay.txt` under `SDL_GetPrefPath("BitRiot", "BitRiot")`, written when Open lobby or Connect is pressed (not by `--host`/`--join`), read tolerantly, written through a temporary file and a rename; the path is looked up the first time an online page needs it, so a player who never opens one gets no folder. Native only: netplay is not built for the web, so nothing there compiles it (brief 12's IDBFS can reuse `NetPrefs`).
10. **Text input** is on only while a text field has the focus, so the game's own keys never go through an input method.
11. **Tab navigation skips hidden controls** (the host's buttons are hidden for a peer); the rest of the menus are unchanged.

**Deviations, and corrections to the brief and code-facts**

- The brief says the lobby slot shows "a remote peer's name, with its local index": it shows `Bob 1/2` for a machine with two players; the viewer's own seats say `(you)`.
- Ping in the InfoPanel and controller-friendly text entry (both optional in the brief) were not done.
- Brief and code-facts said the stall box and status are "07's SDL_ttf box and the title bar": both are gone; code-facts §8, §12 and the header note are updated. `NetGame::lobbyTitle()`/`lobbyText()` no longer exist.
- **A gap in 07 fixed**: when a session ended (the host left, a removal, a desync) the player's own `GameSettings` stayed overwritten by the last match until the page's Leave was pressed; `NetGame::pump()` now restores them as soon as the session is over (tested).
- `LEAVE`'s decoder accepted any reason byte; it now refuses one above 4.
- A document in this project has no default stylesheet: `netoverlay.rml` showed nothing until `body` had a `font-family` and an explicit size (both are in `code-facts` §8 now).

**Follow-ups**

- **12 (browser).** `NetPrefs` is a plain `key=value` file; the browser needs IDBFS (or localStorage through a hook) and a room-code field next to the address. `NetResolve` needs a browser alternative (`vnet_resolve_host` is `VNET_ENOTSUP` there). The lobby pages are not netplay-specific beyond `NetGame`.
- **13 (hot-join).** `REJECT_IN_PROGRESS`'s page says "join between rounds"; with hot-join it goes. `LEAVE` reason 4 and `kick()` exist for a host that wants to remove someone mid-round (today: lobby only). A joiner in the lobby phase "between rounds" sees a "waiting for the host" page with no seat of its own until the next `START`.
- **Not done:** moving a peer to another seat; resetting ready when the host changes the settings; chat; ping per peer in the InfoPanel; names for mid-round takeovers in the "left" toast; a pad or on-screen way to type an address; a server browser (out of scope).
- **Untested glue** (needs a display): `GameEngine::netIdleFrame`/`netFrame`/`updateNetOverlay` as a whole, the main menu hook, `MenuSDLRenderer`'s text input and overlay drawing, the scoreboard buttons. All were driven under Xvfb as listed above.
- **Known limit**: the sanitiser keeps any well-formed UTF-8 a peer sends in its name (up to 24 bytes); FreeMono has no glyph for most of it, so such a name shows blanks. It cannot overflow or break the page.

**Review fix round** (an Opus review of bc77dab: merge after fixes; new commits on top, no history change)

1. `cycleTeam` wrap tested (2 seats: team 1, 0, 1); `% MAX_SLOTS` now fails it.
2. A host that waits for ready could never start with `bitriot_headless join`: a joining runner now says ready once it is in the lobby; `bitriot_headless host --require-ready 1` (new) makes the process test `AHostThatWaitsForReadyStartsWithAHeadlessPeer` possible; without the `setReady` it times out and fails.
3. The round and phase checks in `onLobby` are tested with a `FakeHost` (a raw-socket host and a real peer session); removing either fails it.
4. Cancel while the session is CONNECTING returns a joiner to the join form (address kept), like Cancel during the lookup.
5. Removed-peer memory (30 s): if every `LEAVE` reason 4 is lost, the host answers the peer's later packets with it. Tested with 100% loss during the kick.
6. `SDL_StopTextInput()` once in `initGui`.
7. The main menu hook now inserts the two buttons into mainmenu.rml's own list and moves the later tab indexes up by two (`mainmenuitems` id removed); checked under Xvfb and by the web smoke test.
8. The orphan comment in GameEngine.h and the unused `BASE_TITLE` are gone.
9. The host form's note names the port in the field and follows it (`onchange`). **Note on settings:** the lobby's map, blocks, enemies, player-count and team controls are *new lobby buttons* (`NetGame::cycleSetting`, the same steps and limits), not the Game Options screen's own controls; the host's local Game Options are only where the lobby starts from.
10. `describeEnd` names no number of seconds.
11. The settings file's temporary name includes the process id. Follow-up: `std::rename` does not replace an existing file on Windows; handle it when netplay is enabled for MinGW (no Windows code was added).

Results after the fix round: GCC and Clang `bitriot_tests` **678**, `bitriot_sim_tests` **532**; ctest 3/3 (the process test now has 8 cases); `-DBITRIOT_NETPLAY=OFF` 526 and 412; web smoke 41 of 41; 200-seed sweep (GCC, tree not rebuilding) 0 failing seeds on both binaries; build id still `0x6767612d056ba8af`. "Ready survives setting changes" stays a documented follow-up.

**Play-test** (pending; two windows on one machine are enough; a second machine and a pad make it better). The commands are untested on Arch (the container is Ubuntu); the dependencies are the same as brief 07's.

```bash
# one-time: dependencies, as in brief 07 (RmlUi 4.3 built from source with the two flags Arch's newer tools need)
sudo pacman -S --needed base-devel cmake git sdl2 sdl2_mixer sdl2_ttf sdl2_image freetype2 mesa
git clone --depth 1 --branch 4.3 https://github.com/mikke89/RmlUi.git /tmp/RmlUi
cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_SAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_LUA_BINDINGS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"
cmake --build /tmp/RmlUi/build -j"$(nproc)" && sudo cmake --install /tmp/RmlUi/build && sudo ldconfig

# the game (the `claude` branch once this is merged; --recurse-submodules fetches ext/vnet)
git clone --recurse-submodules --branch claude https://github.com/VenKamikaze/BitRiot.git ~/bitriot-net
cmake -S ~/bitriot-net -B ~/bitriot-net/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-net/build -j"$(nproc)" --target BitRiot
cd ~/bitriot-net/build

# two windows, each with its own saved-settings folder so they do not overwrite each other's name and address
XDG_DATA_HOME=/tmp/alice-data ./BitRiot      # terminal 1
XDG_DATA_HOME=/tmp/bob-data   ./BitRiot      # terminal 2 (drag the windows apart; click a window before typing in it)
```

Keys in every menu: Up/Down move, Return presses, Esc goes back, the mouse works too. In a text field type, Backspace edits, Return or Down goes to the next field.

1. **Host.** Window 1: *Host online game*. The name field has the focus: type `Alice`; Down to *UDP port* (7777), Down to *Players here*, Down to *Open lobby*, Return. The lobby shows `Alice (you)` as seat 1 (yellow), a `Computer` seat, open seats dimmed, *Players auto*, the host's setting buttons, and "No one else has joined yet...". Check: no port-forwarding note matters on one machine; leave 7777.
2. **Join.** Window 2: *Join online game*, the *Host address* field has the focus: type `127.0.0.1:7777`, Up, type `Bob` in the name, Down, Down, Down to *Connect*, Return. It shows "Connecting..." (with Cancel) for a moment, then the lobby: `Alice` (host), `Bob (you)` with a ping of a few ms and "waiting". Window 1 shows `Bob` with a remove button (`x`), and says "Waiting for Bob to be ready." with *Start match* greyed.
3. **Ready.** Window 2: Return on *Ready* (it has the focus): it reads *Not ready*, Bob's row shows `ready` on both windows, window 1 says "Everyone is ready." and *Start match* is enabled. Press Return on *Not ready* once and see it go back.
4. **Settings and teams.** Window 1: Down moves along *Width, Height, Blocks, Enemies, Players*, then the team buttons, then the remove buttons; Return changes the focused one. Watch window 2's header (`Map 17 x 13, ...`) and its Team column follow within half a second. Set *Players* to 5 or 6: the height is raised to 19 by itself. Ready is not cleared by a change.
5. **A match.** Window 1: Up (back to *Start match*), Return. Both windows start the same round: Alice is player 1, Bob player 2 (WASD + Left Ctrl/Left Shift in each window), the rest bots. The bottom-left line reads `NET host D=n` / `NET peer D=n ping n ms`. Pictures should stay identical. To see the stall overlay: in a third terminal `kill -STOP $(pgrep -n BitRiot)` freezes the newest window for a few seconds: the other shows a red `Waiting for ...` box after half a second; `kill -CONT` frees it.
6. **Leaving.** In window 2 press Esc: it goes to the main menu and window 1 shows `Bob left - now AI` for a few seconds, and Bob's player carries on as a bot.
7. **Scoreboard.** Let a round end (or restart and stand still). Window 1: *Next round*, *Back to lobby*, *Main Menu*; window 2: *Waiting for the host...* (greyed) and *Main Menu*. *Back to lobby* on window 1: both go back to the lobby with ready cleared; Bob presses Ready again; change a setting; *Start match*. Also try *Next round* (starts at once on both).
8. **Remove.** In the lobby, window 1: Down to Bob's `x`, Return. Window 2 shows "You were removed" with *Back*, which opens the join form with the address filled in; Connect again to rejoin.
9. **Errors** (window 2, Join): `127.0.0.1:7999` (nothing listening) gives "The host did not answer" after ten seconds and Cancel works while it waits; `bad host` gives the sentence about the address on the form; `no-such-host.invalid:7777` gives "Cannot find the host" (the menu stays responsive during the lookup); while window 1 is in a round, a join gives "A match is already in progress"; window 1 pressing Esc in the lobby gives window 2 "The host left"; `kill -9` on window 1 gives "Lost the connection to the host" after ten seconds. For the build-id and data pages, run window 2 from a copy of the tree whose `data/entity.txt` has one number changed ("Different game data", both hashes) or from a different commit ("Different game build", both build ids, and which protocol is newer if they differ).
10. **Saved settings.** Quit both, start them again, open *Join online game*: the name and the last address are filled in; `cat /tmp/bob-data/BitRiot/BitRiot/netplay.txt` shows four `key=value` lines. Delete or corrupt the file and the form opens with the defaults.
11. **Your own settings survive.** Before step 1 set *Game Options* in window 2 (for example map width 21). After hosting or joining and leaving (or being removed, or the host leaving), *Play* a local game: the map width is still 21 and the players are as you had them.
12. **Regression.** `./BitRiot` with no flags is the game as before (the main menu has the two new entries between *Play* and *Game Options*); `./BitRiot --host 7777 --players 4 --name Alice` and `./BitRiot --join 127.0.0.1:7777 --name Bob` still open straight into the lobby (the host does not wait for ready); `./bitriot_headless host|join` is unchanged.

Record the result here when it comes back.
