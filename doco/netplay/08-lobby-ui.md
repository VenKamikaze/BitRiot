# 08 — Lobby UI

- **Repo:** BitRiot
- **Branch:** `netplay/08-lobby-ui`
- **Depends on:** 07
- **Status:** todo

## Goal

Let players host and join online games from the menus, without command-line flags. They should be able to see who is in each slot, set teams and settings (host only), and see network state during play: waiting for a player, desync, a player leaving.

## Read first

- Brief 07, including its Outcome: the CLI flow, the `LOBBY`/`START` messages and the session API.
- `assets/menu/*.rml` and `*.rcss`, in particular `mainmenu`, `playerselect` and `gameoptions`.
- `src/RmlUI/` binders and event handlers (`PlayersSelectBinder`, `EventHandlerPlayerSelect`, …), and `src/MenuSDLRenderer.cpp`.
- `tests/support/RmlLayoutHarness.*` and `tests/ScoreboardLayoutTest.cpp`, which show how menu layouts are asserted headlessly.
- Today's local flow: main menu → player select → game options → start.

## In scope

1. **Main menu:** add "Host online game" and "Join online game" alongside the existing local play.
2. **Host screen:**
   - The listen port, with a sensible default and a note about which UDP port to forward.
   - Then an **online lobby** based on the player-select screen, where each slot shows one of:
     - host-local
     - a remote peer's name, with its local index
     - AI
     - open
   - Each slot also shows its team and ready state.
   - The host edits settings with the existing game-options controls, and starts once everyone is ready.
3. **Join screen:**
   - An address field (`host:port`) and a name field. Controller-friendly text entry is a nice-to-have; keyboard is enough. Also choose how many local players to bring.
   - Then the same lobby, read-only except for the peer's own ready state.
   - Clear errors for rejection: version, data mismatch, full.
4. **In-game overlays,** drawn by the game renderer or as a small RmlUi document over the game:
   - "Waiting for <name>…" after about 0.5 s of stalling.
   - "<name> left — now AI".
   - "Desync at tick N — dumps written to <path>".
   - Ping per peer in the InfoPanel is optional.
5. **Between rounds:** the scoreboard shows "Waiting for host…" on peers. The host gets a "Next round" button and can return everyone to the lobby.
6. **Settings isolation.** Brief 07 snapshots the joiner's local `GameSettings` before applying the host's settings, and restores them on leaving. Make sure the menu flow goes through that path, so a joiner's own local-play settings survive an online session.
7. **Persist the last used name, address and port.** This means adding a small settings file, because nothing is written to disk today. Keep it simple, under the user config dir via `SDL_GetPrefPath`.

## Out of scope

- Relay and room codes (brief 12 adds "Join by room code" for the relay).
- Matchmaking and server browsers.
- Chat.

## Tests

- **Layout tests** for the new screens, using `RmlLayoutHarness`: the lobby slot table lines up for 2–6 slots, and long names are truncated or wrapped without overflowing.
- **Binder tests:** a `LOBBY` state with mixed slot kinds maps to the expected rows and flags.
- **Flow tests:** driving the menu state machine without SDL where feasible. Otherwise document a manual test script for the user, with exact steps for two windows on one machine.

## Acceptance criteria

- The host/join flow works end to end from the menus. The user play-tests it, following the documented steps.
- Local play is unchanged.
- New screens have layout tests.
- Errors are visible and specific.

## Outcome

*(fill in when done)*
