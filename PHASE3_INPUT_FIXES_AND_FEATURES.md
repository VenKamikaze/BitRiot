# Input bugs + explicit per-player device assignment (player-select)

# BitRiot Input Refactor — Continuation Handoff

> Drop this into a new session to resume. Pairs with the full design plan at
> `~/.claude/plans/map-how-input-and-idempotent-pike.md` and project memory
> `~/.claude/projects/-home-msaun-personal-git-BitRiot/memory/bitriot-input-refactor.md`.

## What this project is
BitRiot — C++ re-implementation of Amiga *BugBomber* (Bomberman-like), SDL2 +
RmlUi menus, ~19.5k LOC, Apache-licensed. The player-select screen is the main
unfinished feature and the source of the input/controller-assignment bugs.

## Process constraints (carry forward)
- Work on a **separate branch**; do **not** commit unless explicitly asked.
- Leave the `ext/RmlUi/RmlUi` submodule pointer **out** of commits.
- Commit messages end with:
  - `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`
  - `Claude-Session: https://claude.ai/code/session_0144xH7EiP26Tq3CP1p3aRsj`
    (this is from the prior session — use the current session's footer when
    actually committing).
- No physical gamepad on this machine → controller paths can't be self-verified;
  keyboard paths verify headless (display is available). Flag controller testing
  for the user.

## Branch state
Branch lineage off `feature/playerselect_test_clion`:
- `fix/build-gcc16-sdl2` — modern GCC 16 / SDL2 build fixes
  (`-include cstdint`, `SDL_VIDEO_RENDER_OGL`→`SDL_VIDEO_OPENGL` guard-swap in
  `src/main.cpp:67` and `src/RmlUI/glue/RenderInterfaceSDL2.h:35`, CMakeLists
  modernization). Commit `4edf272`.
- `fix/input-keyset-pool` (current, off the build branch) — input bug work.

Neither branch merged. Build command:
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j`

## Context

Branch `feature/six-players` (6-player work uncommitted). A real gamepad is now plugged
into the dev machine, which surfaced two bugs in previously "gamepad-gated" code and
motivates the long-recorded feature (PHASE2_HANDOFF.md step 4 "Future work"): explicit
per-player device assignment on the player-select screen.

**Bug 1 — P1 attaches to keyboard, not the controller.** There is *no code path* that
attaches the primary (menu-navigating) controller to a player. `processControllerButtonDown`'s
primary branch (`src/RmlUI/events/EventHandlerPlayerSelect.cpp:117-171`) only emulates menu
keys; the sole attach path `controllerJoin` (`:76-110`) runs only for a *second* pad and can
never bind slot 0 (skipped as "selected"; `attachPlayerToController` requires `!player.human`).
So P1 has `attachedControllerId == -1` and `InputHandler::setPointers` default-seeds it WASD
(`src/InputHandler.cpp:74-78`).

**Bug 2 — START on a pad segfaults in-game.** `src/MickSDLUtil.cpp:13` declares
`static map<SDL_JoystickID, SDL_Haptic*>* hapticDevices;` which is **never allocated**;
`rumbleController` dereferences it at `:61`. Reached from the unbound-controller START rumble
(`src/GameRunningSDLInput.cpp:165`) and from rumble-on-damage (`src/PlayerCharacterEntity.cpp:124`).
`SDL_HapticOpenFromJoystick` is called *only* here (`MickSDLUtil.cpp:69`). A **second**
`hapticDevices` in `GameRunningSDLInput.cpp:18` IS allocated (`:31`) but nothing ever inserts
into it — its CONTROLLERDEVICEREMOVED "cleanup" (`:274-281`) reads a permanently-empty map.

**Bug 3 (latent) — device-index vs instance-id mix.** `PlayersSelectBinder::mapControllersData`
(`src/RmlUI/binders/PlayersSelectBinder.cpp:~234`) stores the loop **device index** as
`Controller.index`, while `controllerDeviceAttached` stores the **instance id**; button events
carry instance ids. `attachPlayerToController` (`:~294`) and `detachPlayerFromController`
(`:~342`) deref `getControllerFromIndex(instanceId)` with **no null check** → wrong lookup /
null deref once ids diverge (any replug). Also `gamecontroller(controllerId+1)-ready.png` uses
the raw instance id — bogus filename after replug.

**Decisions confirmed with user:** (1) primary controller **auto-attaches to Player 1** at
player-select load when P1 is human/device-less and the pad is free (zero-click Bug-1 fix;
still cyclable back to keyboard). (2) Controller label is ordinal **"Pad 1" / "Pad 2"**, not
the SDL device name.

## Constraints (carried from handoff)

- No commits unless asked; keep `ext/RmlUi/RmlUi` submodule pointer out of any commit.
- All model mutations through `GameSettings` setters (mirrors re-sync); index the canonical
  model by `getTeam()-1`, never `getID()`.
- Build: `cmake --build build -j` (configure exists). Verify against `build/BitRiot`,
  **never** stale `target/`.
- Never regress the 4-player keyboard flow (defaults must still yield P1=WASD…P4=IJKL).

## Part A — Bug fixes (standalone, land first)

### Step 1 — Unify hapticDevices, fix the START segfault
`src/MickSDLUtil.cpp/.h`:
- Replace the never-allocated file-static pointer with a Meyers accessor
  (`static map<SDL_JoystickID, SDL_Haptic*>& hapticDevices() { static map<...> m; return m; }`);
  update the three uses in `rumbleController` (lines 61, 63, 90).
- In `rumbleController`, null-guard `SDL_JoystickFromInstanceID(joystickID)` before
  `SDL_JoystickIsHaptic` (pad may have been unplugged) → return false.
- Add `static void closeHaptic(SDL_JoystickID)` — find → `SDL_HapticClose` → erase.

`src/GameRunningSDLInput.cpp`:
- Delete the duplicate `hapticDevices` static (line 18) and its `new` (line 31).
- CONTROLLERDEVICEREMOVED branch (`:274-281`): replace the dead map dance with
  `MickSDLUtil::closeHaptic(event.jbutton.which)`.

### Step 2 — Instance-id consistency + null-guards in the controller list
`src/RmlUI/binders/PlayersSelectBinder.cpp`:
- `mapControllersData()`: store `SDL_JoystickGetDeviceInstanceID(i)`, not the device index.
- `attachPlayerToController` / `detachPlayerFromController`: null-guard the
  `getControllerFromIndex` result (log a warning if null).
- Add `getControllerOrdinal(instanceId)` helper on the `Controllers` struct (1-based position
  in the connected list); use it for `gamecontrollerN-ready.png` selection instead of the raw
  instance id. (Reused for "Pad N" labels in Part B.)

## Part B — Feature: explicit per-player device assignment

Each slot's existing "Control:" button becomes a cycle:
**CPU → Keyboard 1..4 (free sets only; omitted for slots 5/6) → each free connected pad → CPU**.
Slots 5/6 thereby get the recorded intent "allow Human, require a pad". P1 may be CPU (already
possible today via `togglePlayerAI`).

### Step 3 — Model + assignment core (binder)
- `src/PlayerSetupStruct.h`: add `std::string deviceLabel = "CPU";` to `Player`.
- `src/RmlUI/binders/PlayersSelectBinder.cpp/.h` — new statics:
  - `isKeyboardSetTaken(setId, exceptPlayer)` / `isControllerTaken(instanceId, exceptPlayer)`.
  - `assignPlayerControl(playerIndex, ControlChoice)` — the **single write path**
    (tagged value: CPU | KEYBOARD(set) | CONTROLLER(instanceId)). CPU → clear both ids +
    `setPlayerAI(i,true)`; keyboard → clear controller id, `setPlayerKeyboard`, `setPlayerAI(i,false)`;
    controller → clear keyboard id, `setPlayerController`, `setPlayerAI(i,false)` + update the
    controller strip images (new pad → ready image, previously-held pad → `gamecontroller-join.png`).
    Ends with `refreshDeviceLabels()` + `refreshMapConfig()` + DirtyVariable `players`/`controllers`.
  - `refreshDeviceLabels()` — per player: `!human` → "CPU"; `attachedKeyboardId>=0` →
    "Keyboard N" (id+1); `attachedControllerId>=0` → "Pad N" (via `getControllerOrdinal`).
  - `normalizeAssignments()` — (a) auto-attach: primary pad → slot 0 when slot 0 is human with
    no device and the pad is unassigned; (b) any other human with both ids -1: slots 0-3 → first
    free keyboard set (prefer set == slot), slots 4-5 → CPU; (c) drop stored controller ids no
    longer present in `controllersData` (detach → CPU, or keyboard fallback for slot 0).
- Register `deviceLabel` via `RegisterMember` in `setupDataBinding`.
- Reroute existing paths through the core: `attachPlayerToKeyboard` gains an
  `isKeyboardSetTaken` refusal; `attachPlayerToController` gains `isControllerTaken`;
  `detachPlayerFromController` delegates (slot-0 keyboard fallback lives here);
  `controllerDeviceAttached/Removed` also call `refreshDeviceLabels()` (ordinals shift).

### Step 4 — Cycle callback + RML/RCSS
- New bound callback `cyclePlayerControl` (replaces `togglePlayerAI` binding; delete its
  slots-5/6 no-op branch — the candidate list simply has no keyboard entries for
  `playerId >= MAX_KEYBOARD_PLAYERS`). Build ordered candidates
  `[CPU, free-or-own sets (slots 0-3), free-or-own pads]`, advance with wrap, `assignPlayerControl`.
- `assets/menu/playerselect.rml` control button: `data-event-click/keydown="cyclePlayerControl"`,
  `data-rml="player.deviceLabel"`; keep id/class/tabindex/row/col attrs (controller grid-nav
  depends on them).
- `assets/menu/playerselect.rcss`: widen `.joinplayer` for "Keyboard 4". Icons are optional
  cosmetics — keep out of the critical path.

### Step 5 — psinit hook + start gate
- `EventHandlerPlayerSelect::ProcessEvent` `psinit` branch: call
  `normalizeAssignments()` + `refreshDeviceLabels()` after `refreshMapConfig()` (post-game
  visits then show the actually-seeded sets).
- `GameSettings::canStartGame()`: also false if any player is
  `human && both ids == -1 && id >= MAX_KEYBOARD_PLAYERS` (undrivable human — defense in depth
  behind the existing `EventManager.cpp:101` guard). Extend `refreshMapConfig()`'s
  `playerLimitMsg` to name this cause.

### Step 6 — Collision-free default seeding
`src/InputHandler.cpp:74-78`: replace `keyboardSetIndex = canonicalIndex` with "lowest set not
assigned to any player in the canonical model and not handed out earlier in this loop" (local
`std::set<int> usedSets` seeded from all `attachedKeyboardId`s). Keep the `setPlayerKeyboard`
write-back. Identical result to today when nothing is explicitly assigned; prevents
double-WASD when e.g. P2 explicitly owns set 0.

### Step 7 — controllerJoin polish
`EventHandlerPlayerSelect::controllerJoin`: bail early if the pad already owns a slot
(`getPlayerForControllerId`). Keyboard/second-pad implicit hot-join otherwise stays, now
uniqueness-checked via the core.

**Ordering:** 1, 2 standalone first. 3 before 4 (4's RML references `deviceLabel`). 6 before
play-testing 4. Each step build-green.

## Verification

Keyboard (self-verifiable):
1. No pad: player-select shows P1 "Keyboard 1", P2-4 "CPU"; WASD drives P1; "Play again" same.
2. Cycle uniqueness: P2 can't take "Keyboard 1" while P1 holds it; after P1 → "Keyboard 2",
   P2 can; in-game no double-drive.
3. Hot-join RCTRL → slot 2 "Keyboard 2"; a taken set is refused.
4. 6-player map-height gate still works; post-game revisit shows seeded labels.

Gamepad (user):
5. **Bug 1:** launch with pad → P1 shows "Pad 1" automatically; start; pad drives P1.
6. Reassign: P1 → "Keyboard 1", pad → P3 via cycle; in-game P3 on pad.
7. **Bug 2:** in-game START on unbound pad → no segfault, rumble, 2.7 selector opens; damage
   rumble works; mid-game unplug → haptic closed, player reverts (2.6).
8. **Bug 3:** unplug/replug at player-select → strip correct, attach/detach no crash.
9. Second pad A-press joins first free column; slots 5/6 offer CPU + pads only.

## Critical files
`src/MickSDLUtil.cpp/.h` · `src/GameRunningSDLInput.cpp` ·
`src/RmlUI/binders/PlayersSelectBinder.cpp/.h` · `src/PlayerSetupStruct.h` ·
`src/GameSettings.h` · `src/InputHandler.cpp` ·
`src/RmlUI/events/EventHandlerPlayerSelect.cpp` · `assets/menu/playerselect.rml/.rcss`
