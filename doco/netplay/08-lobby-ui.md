# 08 — Lobby UI

- **Repo:** BitRiot
- **Branch:** `netplay/08-lobby-ui`
- **Depends on:** 07
- **Status:** todo

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

*(fill in when done)*
