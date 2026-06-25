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

## Phase 1 — DONE ✅ (commit `5097d40`, user play-tested OK)
Bug A: `InputHandler` is a singleton reused across games, but `setPointers()`
consumed the keyset pool (`m_availableKeySets.front()` + `erase(begin())`) every
game while `setAvailableKeys()` only ran in the constructor. Game 2 → `.front()`
on empty vector → UB → corrupted keyboard assignment.
Fix (live in `src/InputHandler.cpp` `setPointers`): clear + rebuild the pool each
game (`m_availableKeySets.clear(); setAvailableKeys();`). Verified: P1=WASD,
P2=arrows, P3=numpad, P4=IJKL correct on 2nd/3rd games.

## The core problem Phase 2 fixes
Assignment lives in **two disconnected worlds**:
- **Menu world:** `PlayersSelectBinder` file-scoped `vector<Player>`
  (`PlayerSetupStruct.h`: `human`, `attachedControllerId`, `attachedKeyboardId`).
- **Game world:** `PlayerCharacterEntity.attachedController` +
  `InputHandler.m_keyMap`, re-derived first-come-first-served at runtime.
- Only `GameSettings` crosses the boundary, and it carries **counts / genders /
  AI flags only — never device assignments**. So menu controller choices are
  dropped at game start.

User chose to **centralise**: `GameSettings` owns one authoritative
`vector<Player>`, seeded into the game at `GAME_INIT`, mutated by hot-join.

## Current state at a glance (read this first)
- **2.1 + 2.2 are COMMITTED** (`f7d3c77`) — were play-tested before commit.
- **2.3 is COMMITTED** (`a638280`) — build-verified + keyboard startup smoke-test;
  controller-effect path NOT self-verifiable (no gamepad), flagged for user.
- **2.4 is COMMITTED** (`a3fde81`), **crash found in real-play, fixed in
  `ca8577f`.** Build-verified + startup smoke-test only — the fix is
  analysis-verified, NOT yet reproduced (crash only manifests game1→"Play
  again"). **Repro to confirm with user:** play as player 3 (numpad), finish the
  round, hit Play again → expect no crash AND numpad still drives player 3.
  - **CRASH ROOT CAUSE / mental-model correction (this exact wrong model caused
    the bug):** `getID()` is a **non-resetting global entity id** from
    `EntityFactory::m_idCount` (a Meyers singleton, init 1 once, never reset) —
    it is ~1980 by the 2nd game, NOT a player slot. Index the canonical model by
    **`getTeam() - 1`** (1-based slot), never `getID()`. `getID()` is only an
    opaque per-entity key (m_keyMap / m_action* maps). Logic is
    backward-compatible with old pop-order: P1=WASD/P2=arrows/P3=numpad/P4=IJKL.
  - **Pre-existing latent bug — now FIXED (`52aa54c`):**
    `processKeyboardInput`'s `setSelection(.., getID() + 1)` had the same
    getID()-as-slot assumption; bounds-guarded so it silently no-opped (cosmetic
    in-game item-selection icon) in game 2+ rather than crashing. Now passes the
    1-based slot `getTeam()`. Cosmetic; needs user real-play to confirm.
- **2.5 DONE + COMMITTED (`b40b2c4`) + PLAY-TESTED OK (2026-06-25)** — menu keyboard-join.
  The `+1` off-by-one fix at `EventHandlerPlayerSelect.cpp:91` rode in the same
  commit but is **static-analysis only, gamepad-gated** (controller path
  unverified). **Next up: 2.6** (mid-game controller hot-join write-back —
  gamepad-gated).
- **2.4/crash-fix/selection-fix real-play: CONFIRMED OK by user (2026-06-25).**
  That outstanding confirmation is now closed.
- **2.8 keyboard hot-join indicator DONE + keyboard-play-tested OK (2026-06-25),
  NOT committed.** Single recoloured keyboard asset on the InfoPanel (gray while
  selecting → player colour on takeover + persistent badge for keyboard players).
  **Two open follow-ups:** icon is too large (shrink, size TBD) and controller
  hot-join should be ported to the same colour-key recolour. See step 2.8 below.
  After 2.8 the input-refactor work pauses for the new feature roadmap (see
  **Next up** near the end).
- **GOTCHA that cost a session:** there is a **stale `target/BitRiot`** binary in
  the tree with the OLD input bugs. The live build is **`build/BitRiot`** (built
  via the command below). Always run/verify against `build/` — a game-2
  "keyboard dead" report turned out to be the old `target/` binary, not a real
  regression. Consider deleting/ignoring `target/` to avoid the trap.
- **Gamepad-pending items** (dev machine has no pad — flag for user testing):
  the 2.3 controller seed, the 2.5 `+1` controller-join fix, and all of 2.6
  (mid-game hot-join). Keyboard paths verify here.
- Throwaway headless unit test for the 2.1 setter logic lived at
  `build/test_gamesettings.cpp` (28/28 passed; deleted — `build/` is regenerable).
  Re-creatable by linking CMake's objects minus `main.cpp.o` if needed again.

## Phase 2 — IN PROGRESS. Steps:
1. **2.1 Canonical model in GameSettings.** ✅ DONE (build-verified + headless
   unit test 28/28 + real-play tested). Added `std::vector<Player> m_players`
   as single source of truth + accessors `getPlayers()`, `getPlayer(int)`,
   `setPlayerController(id,ctrlId)`, `setPlayerKeyboard(id,setIndex)`.
   `getPlayerGenders()`/`getPlayerAIs()` still return `std::vector<bool>*` to the
   now-**derived mirror members** `genders`/`playerAIs`; private `syncMirrors()`
   rewrites them from `m_players` after every mutation. Constructor seeds
   `m_players` then calls `syncMirrors()`. `setNumberOfPlayers`/`setPlayerAI`/
   `setPlayerGender` rerouted through `m_players` (+`syncMirrors`);
   `setNumberOfPlayers` preserves existing entries on grow (so device
   assignments added in 2.2/2.3 survive a player-count change), truncates on
   shrink. All existing callers/signatures unchanged.
   - **Disclosed behaviour change (cosmetic):** default in-game face genders
     shift `[M,M,F,M]→[M,F,M,F]`, matching what the menu already displays. Which
     face bitmap loads; no gameplay impact.
2. **2.2 Menu writes canonical model.** ✅ DONE (build-verified + headless startup
   check + real-play tested for keyboard/menu; controller-join still pending
   gamepad). `Players::players`
   is now a **reference** to `*GameSettings::getInstance()->getPlayers()` (RmlUi
   binds to the same canonical vector). All mutations route through a single
   write path — the GameSettings setters — so the genders/playerAIs mirrors stay
   in sync: `toggleNumPlayers`→`setNumberOfPlayers` (grow/truncate-preserve),
   `togglePlayerAI`→`setPlayerAI`, `togglePlayerPortrait`→`setPlayerGender`
   (+portrait set on the record), `attachPlayerToController`/
   `detachPlayerFromController`→`setPlayerController`+`setPlayerAI` (the
   human-flip now also re-syncs the mirror — **2.1 carry-over flag closed**).
   `mapPlayersData()` is now a no-op (canonical model self-seeds; previously it
   double-seeded). Static-init safe: the reference member forces GameSettings
   construction pre-`main`, but its only side effect is `access()`-based file I/O
   (`checkAssetAvailable`), not SDL — verified no startup crash, 4 face loads not 8.
   - **Disclosed behaviour changes:**
     - **Wrap now preserves, not resets.** Cycling player-count past MAX back to
       MIN keeps players 0..MIN-1 with their toggled AI/gender/device state
       (old code `clear()`ed + rebuilt defaults). This is the intended preserve
       guardrail; arguably better UX (no silent loss of settings).
     - **`.at()` instead of `[]`** in `attachPlayerToController`: the existing
       `EventHandlerPlayerSelect.cpp:91` passes `atoi(playerId)+1` as the index.
       **Statically confirmed bug:** `playerselect.rml` sets
       `data-attr-playerId="player.id"` and `player.id` is 0-based (0..3). So the
       `+1` makes controller-join index players 1..4 — the last column hits
       `.at(4)` and **throws** (was silent UB before `.at()`), and every other
       column attaches the *wrong* (off-by-one) player. The `+1` is the real bug;
       its correct fix depends on intended controller-join semantics (it may be a
       botched attempt to skip the always-human player 0). **Resolve in 2.5** with
       gamepad testing — do not blind-fix. Controller path, not headless-verifiable.
3. **2.3 Seed the game at GAME_INIT.** ✅ DONE (committed `a638280`,
   build-verified + keyboard startup smoke-test). `initPlayerCharacters()`
   (`GameEngine.cpp`) copies `attachedControllerId` from the canonical model
   (`getPlayer(i-1)`) onto `PlayerCharacterEntity.attachedController`. The
   keyboard-set-index half was **intentionally deferred to 2.4** (passing an
   index the assignment logic doesn't yet consume would be dead plumbing; 2.4's
   keyboard-by-id reads `attachedKeyboardId` itself). Controller-effect path
   needs a **gamepad** to verify (flagged for user).
4. **2.4 InputHandler keyboard-by-id + mandatory default seeding.** ✅ DONE
   (committed `a3fde81`, build-verified + startup smoke-test). `setPointers()`
   assigns each player's keyboard set by stored `attachedKeyboardId` (0-based
   index into the 4 fixed sets); `m_availableKeySets` is now an indexable table,
   not a pop pool. Human/no-controller players with `attachedKeyboardId == -1`
   get a deterministic set by player index, **written back via
   `setPlayerKeyboard()`** so the model stays authoritative + idempotent across
   "Play again". AI and controller players get no set; `processKeyboardInput()`
   now also skips controller players (closes the 2.3 double-input quirk).
   Backward-compatible (old pop order == positional index). **In-game keyboard
   path needs user real-play to verify** (not headless-driveable here).
   **Crash fix `ca8577f`:** index the canonical model by `getTeam() - 1` (the
   1-based player slot), NOT `getID()`. `getID()` is a non-resetting global
   entity id (`EntityFactory::m_idCount`, ~1980 by game 2) usable only as an
   opaque map key — using it as a model index crashed on "Play again".
5. **2.5 Build menu keyboard-join (net-new). ✅ DONE + COMMITTED (`b40b2c4`) +
   PLAY-TESTED** (user real-play confirmed OK 2026-06-25: RCTRL/KP-PLUS/`[` each
   flip the right slot to Human and play on that set, P1 still WASD). The `+1`
   controller fix rode in the same commit and is still **gamepad-unverified**.
   Player-select MENU feature in `EventHandlerPlayerSelect::processSDLEvent`
   `SDL_KEYDOWN` case. **Design (advisor-confirmed): bind set index → slot index,
   NOT first-free** — first-free collides with 2.4's default seeding (slot 0
   would also default-seed to set 0 → two players on WASD). Set→slot is
   collision-free and gives the traditional WASD=P1/arrows=P2/numpad=P3/IJKL=P4.
   - **Trigger = a keyboard set's ACTION1/ACTION2 key** (LCTRL/LSHIFT, RCTRL/RSHIFT,
     KP_PLUS/KP_MINUS, `[`/`]`) — matches the old commented stub's
     `getPlayerIdForAction1Key/Action2Key` intent and avoids clashing with the
     arrow/Return/Escape menu-nav keys. Non-matching keys fall through to
     `ProcessKeyDown` unchanged (tested arrow/Return path preserved).
   - **Key→set lookup chain (single source of truth = the fixed key table):**
     `EventHandlerPlayerSelect` → `GameRunningSDLInput::getKeyboardSetForActionKey(SDL_Keycode)`
     (translates via `reverseTranslateMap`, **guarded against `end()`** unlike the
     blind `getPlayerIdForKey`) → `InputHandler::getKeyboardSetForActionKey(KEY)`
     (searches `m_availableKeySets` ACTION1/ACTION2 entries — the fixed 4-set
     table, populated at ctor + rebuilt each game, NOT the game-only `m_keyMap`).
   - **Bind write path:** `PlayersSelectBinder::attachPlayerToKeyboard(setId)`
     binds set N → slot N: guards `setId < players.size()`, refuses if that slot
     already has a controller or keyboard set, else routes through
     `setPlayerKeyboard(N,N)` + `setPlayerAI(N,false)` (mirrors
     `attachPlayerToController`'s setter path so the playerAIs mirror re-syncs) and
     `DirtyVariable("players")` so the "Control: CPU→Human" label refreshes.
   - **`+1` off-by-one at L91 FIXED (now ~L97)** — removed the `+ 1`. The column's
     `playerId` attr is already the 0-based canonical index, and the free-column
     scan already skips default-human slot 0 (carries the "selected" class), so
     `+1` bound one slot past the column found and threw `.at(4)` on the last.
     **Static-analysis fix — controller path, NOT verified (no gamepad). Flag for
     user gamepad test.** Kept separate from the keyboard change so they test
     independently.
6. **2.6 Hot-join write-back (MID-GAME). ✅ DONE (build-verified, NOT committed,
   gamepad-gated for user test).** `attachNewControllerToPlayer` /
   `detachController` (`InputHandler.cpp`) update the live entity when a
   **controller is plugged in/out during play**.
   - **CORRECTION to the original plan (advisor-confirmed, discriminating check
     done): 2.6 does NOT write the canonical model (`GameSettings`).** The plan's
     "also update the canonical model" wording assumed a live-model-vs-menu-seed
     split that does not exist in code — there is ONE model (`GameSettings`) and it
     IS the menu seed. `resetGame()` re-reads device/AI state out of it every "Play
     again" (`getPlayerAIs()` L53 → `initPlayerCharacters`; `getPlayer(i-1)->`
     `attachedControllerId` L322-323). Grep confirmed **no reader of the per-player
     device/AI fields during `GAME_RUNNING`** (only `resetGame`/`initPlayerCharacters`
     /`setPointers` + the pre-game menu binder). So writing `GameSettings` mid-round
     has zero current-round benefit and only causes persistence — which violates the
     recorded no-persist decision. The live `PlayerCharacterEntity` list IS the
     current-round model and the existing attach code already mutates it.
   - **The actual 2.6 fix is on the live entity:** `detachController` previously
     only set `attachedController = -1`, leaving a taken-over CPU entity `-1` AND
     non-AI → `processKeyboardInput` stops skipping it but it has no keymap →
     **player freezes** instead of reverting to CPU. Fix: on unplug, revert to AI
     (`m_controlledByAI = true`) **only when the entity has no `m_keyMap` entry.**
   - **Why conditional (regression the unconditional version caused):**
     `attachNewControllerToPlayer` only checks `attachedController == -1`, NOT
     `isControlledByAI()`, so when no CPU slot is free a plugged controller
     **hijacks a human keyboard player** (highest-index alive). E.g. P1 WASD + P2
     arrows, plug pad → grabs P2; unplug → an *unconditional* AI-revert would turn
     P2 into a bot (regression — pre-2.6 P2 kept its keymap and resumed keyboard).
     `m_keyMap` presence is the correct discriminator: CPU/ menu-controller player =
     no keymap → revert to AI (avoids freeze); hijacked human = has keymap → resume
     keyboard. (Do NOT use `getPlayerAIs()` — it would mark the menu-controller
     player non-AI and re-introduce the freeze.) `update()` keys off the flag at
     `PlayerCharacterEntity.cpp:68` and self-heals empty goal/instruction lists.
     `attachNewControllerToPlayer` is otherwise complete; annotated w/ no-persist note.
   - **OPEN design question — RESOLVED by 2.7 (CPU-only):** "should hot-join hijack
     a *human*, or only CPU?" 2.7's `getTakeableSlots` filters `isControlledByAI()`,
     so takeover now targets **AI players only** — a controller can no longer
     hijack a human keyboard player. Consequence: `detachController`'s "hijacked
     human keeps its keymap → resume keyboard" branch is now **dead but kept as
     defensive code** (its comment is updated to say so); the old auto-scan
     `attachNewControllerToPlayer` it referenced is **removed**.
   - **Gamepad repro for user:** plug a pad in mid-game → takes over highest-index
     free alive CPU player; **unplug → that player reverts to CPU (no longer
     freezes)**; also test the 2-human + 1-pad hijack/unplug path (P2 should resume
     keyboard, not become a bot); finish round → "Play again" re-seeds from menu
     intent (hot-join does NOT carry over). Build here; commit only after user
     gamepad play-test.
7. **2.7 Mid-game CPU-takeover selector. ✅ DONE (build + analysis-verified, NOT
   committed). Needs user play-test (keyboard) AND gamepad play-test (controller)
   — same verification tier as 2.4/2.6; the selector code never ran here (the
   smoke-test stops at the RmlUi menu and there is no headless menu→game key-
   injection harness — prior phases rightly skipped building one).** Replaces the old
   implicit highest-index-free-alive auto-scan with an explicit on-portrait
   selector. **Considered bundling with 2.5 and decided against:** 2.5 is a
   *player-select menu* feature (pre-game, RmlUi); 2.7 is an *in-game* HUD
   selector on the live entities during play — different screen/code path/render
   layer. Sibling is **2.6** (both mid-game hot-join). Net-new UX.
   - **Trigger / controls (advisor-confirmed, edge-triggered off discrete *-DOWN
     events, NOT the level-polled gameplay loop):**
     - **Open** = controller START / an UNBOUND keyboard set's ACTION1 key
       (LCTRL/RCTRL/KP_PLUS/`[`). A `gamecontrollerX-notready.png` icon appears
       right of a targeted AI portrait.
     - **Navigate** = controller dpad up/down / the set's UP/DOWN keys — cycles
       through alive AI players free to take over.
     - **Confirm** = same trigger again (START / set ACTION1). Icon flips to
       `gamecontrollerX-ready.png` and the device takes the character over.
     - **Cancel** = controller B/Y / the set's ACTION2 key. Icon disappears, no
       takeover.
   - **Takeover mechanism (the testable crux):** `attachNewControllerToPlayer`
     auto-scan **removed**; replaced by `takeOverSlotWithController(team,ctrlId)`
     (sets `attachedController`+`m_controlledByAI=false`) and
     `takeOverSlotWithKeyboard(team,setIdx)` (**sets `m_keyMap[entityId]` = the
     joining set** + `m_controlledByAI=false` — THIS is what makes the ex-AI
     respond to e.g. the numpad; fully testable here). The joiner controls with
     the device they pressed. 2.6's conditional detach-revert is unchanged.
   - **Selector state** lives in `InputHandler::m_pendingJoins` (one `PendingJoin`
     per device → supports several joiners at once); cleared each game in
     `setPointers`. Rendering via `InfoPanel::setJoinIndicator(team,state)` +
     cached, IMG_Load'd, tile-scaled icons (`InfoPanel.cpp`). Input wired in
     `GameRunningSDLInput`: KEYDOWN branch (guarded `event.key.repeat==0` so a held
     key can't run open→confirm; consumed events skip `setKeyState` so the confirm
     press can't leak into item-placement) and `setControllerInput` (unbound
     controllers route to the selector and eat all presses).
   - **Defaults taken (advisor-OK; react after seeing it):** icon number **X = the
     targeted slot's number** (always 1..4, matches the portrait beside it);
     **ready icon persists** for the round as a "device-controlled" badge;
     selector state is **per-device**; **confirm re-validates** the target is
     still alive+AI+free (handles two joiners racing one slot, or the target
     dying mid-select).
   - **SPEC DIVERGENCE to decide (icon position):** spec says "right hand side of
     the **player portrait**"; it is placed at the panel's far-right top
     (`WIDTH - TILE_WIDTH - 4`, ~4 tiles from the face), NOT immediately adjacent.
     Reason: the cell *immediately* right of the portrait is free for an AI slot
     **pre**-takeover (item-selection indicator is human-only), but once a human
     takes the slot over the persistent READY badge would **collide** with that
     new human's item indicator. Far-right dodges both that and the health bar
     (lower row). If you prefer literal adjacency, accept the post-takeover
     overlap (or hide the READY badge during item placement). **User to decide
     after seeing it.**
   - **BEHAVIOUR CHANGE to flag:** START opening the selector **replaces** the old
     auto-attach-on-any-button (2.6) — plugging in / pressing a pad no longer
     auto-takes-over; you must press START then choose. Handoff says that's
     intended.
   - **Keyboard repro (testable here):** 2 human + 2 AI game; in-game tap KP_PLUS
     → notready icon by an AI portrait; numpad 8/5 shift target; KP_PLUS again →
     ready icon + numpad now drives that ex-AI; KP_MINUS = cancel. Run
     `build/BitRiot`, NOT stale `target/`.
   - **Controller repro (user, gamepad):** START opens, dpad navigates, START
     confirms, B/Y cancels.
8. **2.8 Keyboard hot-join indicator (recoloured single asset). ✅ DONE
   (build-verified, NOT committed) + PLAY-TESTED OK for keyboard hot-join (user,
   2026-06-25).** Adds a keyboard icon to the InfoPanel that recolours ONE source
   asset (`assets/menu/keyboard-64w.png`, 64×35) per state. Mirrors
   `EntityRenderer`'s colour-key idea but **adapted to a 2-tone black-on-white
   silhouette** (the asset is NOT a highlight/base/shadow palette): source
   luminance → alpha (white = the transparency "colour key" → transparent, black
   → opaque target colour, anti-alias greys → partial), and every pixel is
   painted the target colour. Method `InfoPanel::makeKeyboardIcon(r,g,b,maxW,maxH)`
   (static) + `makeTeamKeyboardIcon(slot)`.
   - **States:** light gray `(200,200,200)` while a keyboard hot-join is
     *selecting* a slot (`JOIN_NOTREADY`); the slot's **team colour** once taken
     over (`JOIN_READY`) — colour read from `EntityRenderer::TEAM_BASE_COLOR[slot+1]`
     (same table the sprites use). Also a **persistent player-coloured badge for
     players who start on keyboard**, set in `InputHandler::setPointers` at the
     point the keymap is assigned (human + no controller).
   - **Lazy team-colour build (timing trap):** `TEAM_BASE_COLOR` is zero-filled
     until `EntityRenderer`'s ctor runs, and the InfoPanel is constructed FIRST —
     so the team-coloured icons are built on first render, NOT in the ctor (the
     gray icon IS ctor-built — it is colour-independent). Cached: one gray +
     one per slot, freed in the dtor.
   - **Device threading:** new `InfoPanel::JoinDevice {CONTROLLER, KEYBOARD}`
     param on `setJoinIndicator`; keyboard joins pass KEYBOARD (derived from
     `pj.isController` in openJoin/navigateJoin/confirmJoin). Controllers keep
     their existing `gamecontrollerX-*.png` icons.
   - **Sizing:** 64×35 (~1.85:1) gets a 2-tile-wide box, scaled proportionally
     (→ 58×32) so it does not squash into the square 32×32 tile; right-aligned by
     `icon->w` (`WIDTH - icon->w - 4`).
   - **OPEN follow-ups (user-flagged 2026-06-25, fix later):**
     - **Keyboard icon is too LARGE** — shrink it. Appropriate size TBD (user
       unsure yet; the 2-tile box / proportional scale is the knob —
       `makeKeyboardIcon` maxW/maxH args and the gray-icon call in the ctor +
       `makeTeamKeyboardIcon`).
     - **Controller hot-join should use the SAME colour-key recolour.** Today
       controllers use static per-slot `gamecontrollerX-notready/-ready.png`.
       Port them to the single-asset recolour (gray while selecting → player
       colour on takeover) so both device types behave identically. Will need a
       controller source asset analogous to `keyboard-64w.png`.

### Decided: hot-join does NOT persist into next "Play again" (confirmed)
A mid-game controller hot-join does **not** carry into the next round.
`GameSettings` reflects *menu intent*; the game re-seeds each round from it (menu
stays authoritative — no "controller stuck on player 3 next round" surprise). So
2.6 updates the live model only for the current round, not the persistent
menu-seed.

## Files Phase 2 will touch
- `src/GameSettings.h` / `.cpp` — canonical `vector<Player>`, accessors,
  `syncMirrors()`.
- `src/PlayerSetupStruct.h` — `Player` becomes the canonical record (shared
  header). Note `attachedKeyboardId` is currently a DEAD field.
- `src/RmlUI/binders/PlayersSelectBinder.cpp` — repoint to canonical model.
- `src/GameEngine.cpp` — seed device fields at GAME_INIT.
- `src/InputHandler.cpp` / `.h` — keyboard-by-id + default seeding; hot-join
  write-back.
- `src/RmlUI/events/EventHandlerPlayerSelect.cpp` — implement keyboard-join.

## Key facts / gotchas
- **`GameEntity::getID()` is a non-resetting global entity id**
  (`EntityFactory::m_idCount`, a Meyers singleton init to 1 once). It is NOT a
  player slot — by game 2 it is ~1980. Use `getTeam()` (1-based slot) to index
  per-player/canonical-model vectors; `getID()` is only an opaque unique key.
  (This caused the Phase 2.4 "Play again" crash — fixed `ca8577f`.)
- `InputHandler` singleton created once in `main()`, reused across all games
  (class comment wrongly assumes recreation).
- 4 keyboard sets, `NUM_ACTION_BUTTONS = 6` (UP/DOWN/LEFT/RIGHT/ACTION1/ACTION2).
- `attachNewControllerToPlayer` scans **backwards** (binds highest-index free
  alive player), flips it to human.
- `GameSettings`: `MAX_PLAYERS=4` (TODO 6), `MIN_PLAYERS=2`,
  `GAME_STATE {GAME_INIT, MENU_RUNNING, GAME_RUNNING, GAME_QUIT}`.
- `GameSettings`/most classes live in `namespace std` (legacy quirk).

## Side fix this session: health-bar "stray line" artifact
**✅ DONE (build-verified, NOT committed, needs VISUAL confirmation).**
- **FALSE START (reverted):** first attempt moved the **under-player** map bar
  (`InfoPanel.cpp`, `DRAW_HEALTH_UNDER_PLAYERS` block) from `entityY + TILE_WIDTH
  + 2` up to the bottom of the player's own tile. **User said wrong area + the
  shift was unwanted → fully REVERTED** to the original `+ Map::TILE_WIDTH + 2`.
  The TODO's TILE_WIDTH-vs-HEIGHT hypothesis is still disproven (both == 32), but
  that bar was NOT the artifact.
- **REAL FIX (`Map::renderTileTo`, Map.cpp):** the artifact is in the **right-hand
  InfoPanel** (static portrait health bars). Panel BG tiles are drawn by
  `renderTileTo`, which blits only a **31×31** source (`TILE_WIDTH-1`, a workaround
  for a garbage bitmap edge) and — unlike `Map::render` (Map.cpp:166) — did **not**
  black-fill the cell first. The InfoPanel reuses `m_surface` across frames, so the
  1px seam between every panel tile retained **stale content (prior-frame health-bar
  colour)** → coloured "stray lines between the tile sprites". Fix: black-fill the
  exact `TILE_WIDTH × TILE_HEIGHT` cell before the blit (mirrors `Map::render`).
  Scope: `renderTileTo` has exactly ONE caller (`InfoPanel::drawBGTiles`), so the
  change touches only the panel; the map uses the separate `Map::render` (already
  black-fills). **Needs user visual confirmation in the right panel.**

## Next up (new feature roadmap — beyond the input refactor)
Queued by the user (2026-06-25), in order. These start once the input-refactor
phases (through 2.8) settle. Carry forward the same process constraints (separate
branch, no commit until asked, build each step).
1. **Fullscreen toggle while a game is running**, via the **'Options'** menu.
2. **Map width & height toggles** in the **'Options'** menu — let the user expand
   the map size *before* a game starts. (`GameSettings::getMapHeight()` /
   `getMapWidth()` already feed `InfoPanel`/`Map` sizing; the toggles must drive
   those + the canonical model.)
3. **6-player capability** — `GameSettings::MAX_PLAYERS` 4 → 6 (the existing
   `// TODO 6` at the MAX_PLAYERS gotcha). The **2 extra players (5 & 6) must be
   game-controller-only** — there is not enough room on a keyboard for more sets
   (only 4 keyboard sets exist; see Key facts). So slots 5–6 never get a keyboard
   set; they are controller/AI only.
   - Needs **new character models** for players 5 & 6. The user will source them
     later; **in the meantime RE-USE players 1 and 2's character models** for 5 & 6.
   - Touches a lot of `[MAX_PLAYERS]`-sized arrays + the team-colour tables
     (`EntityRenderer` has 5 team colours = `MAX_PLAYERS + 1`); expanding to 6
     players means 7 teams there. Audit every fixed-size `[GameSettings::MAX_PLAYERS]`
     / `NUM_TEAMS` array and the per-team colour palettes when raising the cap.

## Out of scope (separate follow-ups, not now)
- P1 focus-highlight (blue border) — RmlUi focus/CSS issue in
  `playerselect.rml/.rcss`.
- Two-`hapticDevices`-pointers rumble bug (one uninitialised in MickSDLUtil).

## Verification plan
- Keyboard (headless, here): assign keyboard sets in menu, start, confirm each
  player's keys; "Play again" re-seeds from menu intent.
- Controller (needs gamepad — user or SDL virtual pad): menu controller-join →
  in-game control matches; hot-join take-over of a CPU player still works.
- Build each phase before declaring done.

## Suggested first action next session (starting 2.6)
**Everything through 2.5 is committed and play-tested (keyboard paths).** 2.6 is
mid-game controller hot-join, so it is **gamepad-gated** — the dev machine has no
pad. Build + analysis here; flag for the user to play-test with a controller.

**1. Orient (read these first):**
- `InputHandler::attachNewControllerToPlayer` / `detachController`
  (`InputHandler.cpp`) — the live hot-join/-leave paths that mutate the
  `PlayerCharacterEntity` (attachedController, m_controlledByAI) when a pad is
  plugged in/out **during play**. Currently they touch only the live entity, not
  the canonical model.
- `GameRunningSDLInput::setControllerInput` / `updateEventQueue`
  (`GameRunningSDLInput.cpp`) — where controller add/remove events drive the
  above during a running game.
- `PlayersSelectBinder::attachPlayerToController` /
  `attachPlayerToKeyboard` (`PlayersSelectBinder.cpp`) — the canonical-model
  write path (`GameSettings::setPlayerController`/`setPlayerKeyboard` +
  `setPlayerAI`); 2.6 should update the model the same way, **for the current
  round only**.

**2. Build 2.6:** have `attachNewControllerToPlayer`/`detachController` also write
the canonical model (so the player↔device invariant holds for the live round),
routing through the GameSettings setters. **Decision already recorded (do NOT
re-litigate): a mid-game hot-join does NOT persist into the next "Play again"** —
GameSettings reflects menu intent and the game re-seeds each round, so 2.6 updates
the live model for the current round only. Index per-player vectors by
`getTeam()-1` (the 1-based slot), never `getID()` (the non-resetting global entity
id — see Key facts).

**3. Decision recorded:** 2.7 (mid-game CPU-takeover selector) is **NOT** bundled
with 2.6's plumbing — it is the in-game HUD selector layered on top, its own item.
Schedule with/after 2.6. See the 2.6/2.7 phase entries above.

**4. Verify:** 2.6 needs a **gamepad** — build here, then hand the user a repro
(plug a pad in mid-game → it takes over a CPU player; unplug → that player reverts
to CPU; finish round → "Play again" re-seeds from menu intent, hot-join does NOT
carry over). Build after each change; commit only once the user has play-tested.
