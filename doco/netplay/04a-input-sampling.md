# 04a — Input sampling and `step(inputs)`

- **Repo:** BitRiot
- **Branch:** `netplay/04a-input-sampling`
- **Depends on:** 03b
- **Status:** done (play-test pending)

## Goal

Split human input into two stages:

- **Sample** (local and nondeterministic): read keyboards and controllers into a compact per-slot bitmask, once per stepped tick.
- **Apply** (deterministic, inside the sim): `Simulation::step(const TickInputs&)` turns everyone's bitmasks into movement, selection and placement.

Move per-player input *state* out of `InputHandler` into sim-owned per-slot state. Once this lands, a tick's input is a few bytes that can be logged, replayed or sent.

Control changes (AI↔human), rumble and the InfoPanel highlight are brief 04b.

## Read first

- [`research/code-facts.md`](research/code-facts.md):
  - §6 (input): semantics, quirks, and the state held in `InputHandler`
  - §12: the exact current action rules and `setPointers`
  - §1: ESC as an exception
- `src/InputHandler.{h,cpp}` (`pumpEvents` and `applyInput`, which 03b split out of `processKeyboardInput`; `processPlayerButtons`, `setPointers`) and `src/GameRunningSDLInput.{h,cpp}`.
- The Outcomes of 03a and 03b: the `Simulation` and `frame()` APIs.

## Design

### `PlayerInput`

One byte per slot per tick:

- 6 held bits: UP, DOWN, LEFT, RIGHT, ACTION1, ACTION2
- 2 released-this-tick bits: ACTION1_RELEASED, ACTION2_RELEASED

**Slot index is `playerNumber - 1`** (see the 0/1-based note in `tests/README.md`). Write that on the type.

### `TickInputs`

`std::array<PlayerInput, MAX_PLAYERS>`, plus a vector of control events. The vector stays empty until 04b.

### Apply rules

These must reproduce today's behaviour exactly (code-facts §12), except for the one documented change below. Write them out as a table in the header comment.

- **Movement.** With neither action held, the held direction moves the player, with precedence up > down > left > right.
- **Holding ACTION1** blocks movement and turns directions into the item selection (ROCKET_SPIN etc.). A release places the selection, if the tile has no static entity, and resets to BLOCK.
- **ACTION2 press** selects EGG1 immediately. Directions then select EGG2–EGG5, and a release places the egg.
- A release is only acted on inside its matching action-down state, as today.
- **ACTION1 released while ACTION2 is down, and vice versa:** determine what today's code does and preserve it. Write the finding in the table.
- **Documented change, the same-tick tap.** Today a press and release within one tick is lost, and the stale release can fire later. New rule: if a released bit arrives for an action that wasn't down in the sim's per-slot state, treat it as press then release in that tick. That places the default selection for that action: BLOCK for ACTION1, EGG1 for ACTION2.
  - **Verify those defaults against the code** before relying on them.
  - The sampler must set the held bit and/or the released bit so this is unambiguous. Define how.

### Per-slot sim state

`m_action1Down`, `m_action2Down` and `m_spawnSelection` move from `InputHandler`, where they are keyed by entity id, into per-slot sim state. **Add them to `stateHash()`.**

### Sampling cadence

Events are pumped every `frame()`. Releases and presses accumulate between samples. A sample is taken **once per stepped tick**, so when `canStep()` holds ticks back, input isn't lost.

The global `isAKeyDown()||isAKeyReleased()` gate goes. It only affected when the UI updated, not sim behaviour (code-facts §12).

### Device-to-slot mapping stays local

This covers the keyboard sets, `attachedKeyboardId`/`attachedControllerId`, and `setPointers`. Give the sampler an explicit **local slot mask**, so later only local slots are sampled. Today every human slot is local.

### ESC/quit

ESC stays local and must no longer be an exception thrown through the sim. Make it a flag or return value that `GameEngine` checks. Other `throw` sites can stay.

## In scope

- An `InputSampler` (or the sampling half of `InputHandler`).
- `Simulation::step(const TickInputs&)`, with control events, if any, applied first, then inputs, then the existing tick.
- Moving the per-slot state, and extending the hash.
- Wiring into `frame()`: sample → `step(inputs)`.
- InfoPanel keeps working. Feed it from the sim's per-slot selection for now; 04b tidies this.

## Out of scope

- Control events, the join/takeover/unplug paths, and rumble (04b).
- Recording (05).
- Netplay (07).

## Tests

These use scripted `TickInputs`, with no SDL.

- Held directions move a player, and precedence holds.
- An action held with a direction selects the right item or egg.
- Press-then-release places it.
- A release with no prior down is ignored, except under the documented same-tick-tap rule.
- **Same-tick tap places the default.** This fails on the old code.
- Both actions interacting: whatever the preserved rule is.
- **Determinism:** the same seed plus the same scripted input sequence gives an identical per-tick `stateHash()` across two runs.
- **Sampler unit tests:** a key press plus release between two samples produces the right bits, and samples held back by `canStep()` don't lose events.
- Run a 200-seed sweep.

## Acceptance criteria

- No sim code reads SDL or devices.
- `InputHandler` holds no per-player game state.
- Input can be scripted in tests.
- The rules table and the tap change are documented.
- Local play feels the same apart from the tap fix. This needs a play-test with keyboard and controller, which may be pending at merge.

## Outcome

**Status: done, play-test pending.** Human input is two stages now. The local half samples keyboards and pads into one byte per slot per stepped tick; the deterministic half, `Simulation::step(const TickInputs&)`, turns those bytes into movement, item selection and placement using per-slot state the sim owns and hashes. Everything automated passes on GCC and Clang, the real engine (SDL events and all) was run under Xvfb and its per-tick state hashes equal the harness's replay of the same inputs, and the only intended change in play is the same-tick tap. The user should play it once (steps below), mostly for keyboard and pad feel.

**What landed**

1. **`src/TickInputs.h`** (in `bitriot_sim`, no SDL): `PlayerInput` (one byte: held UP, DOWN, LEFT, RIGHT, ACTION1, ACTION2 in bits 0-5, `ACTION1_RELEASED`/`ACTION2_RELEASED` edges in bits 6-7), `ControlEvent` (an empty placeholder; 04b defines it) and `TickInputs` (`std::array<PlayerInput, MAX_PLAYERS> slot` plus `std::vector<ControlEvent> control`). **Slot is playerNumber - 1**, written on the type. The header comment holds **the rules table** and the encoding (below).
2. **`Simulation::step(const TickInputs&)`** (`step()` with no argument stays, as `step(TickInputs())`, for all-bot rounds and the 150-odd existing tests). Per tick: tick++, then for each slot `applySlotInput` (the control events, when 04b adds them, come first; today `step()` **throws `std::logic_error` if `control` is not empty**, rather than drop one silently), then the existing tick (win check, pool, `runFrame`). The per-slot state is `Simulation::SlotInputState {action1Down, action2Down, spawnSelection}`, reset by `start()`, read through `inputState(slot)`, and **in the state hash** (new section 6b, three fields per slot up to the player count; `STATE_HASH_VERSION` 1 -> 2; spec, schema list and tests updated).
3. **`InputSampler`** (`src/InputSampler.{h,cpp}`, `bitriot_core`, SDL-free): `setButton(slot, button, down)` from the device layer, `sample(slot)` / `sampleTick(localSlotMask)` once per stepped tick. The local slot mask is explicit: slots outside it report nothing and **drop** their pending edges (so a release cannot wait around and fire when the slot later becomes local). `clearSlot` / `clear` / `peek` for takeover, new rounds and tests.
4. **`InputHandler`** is the device-to-slot half now. `pumpEvents()` is unchanged in position (once per frame); keyboard events (`keyboardEvent(KEY, down)`, called by `GameRunningSDLInput::updateEventQueue`) and pad buttons (`setControllerButtonState(slot, ...)`) go to the sampler for the slot that owns that key set or pad. `sampleTick()` returns the `TickInputs` (local slot mask: every human slot, as today). **`quitRequested()`** is the ESC flag (`GameRunningSDLInput::doQuit()`); **`InputException.h` is deleted** and main.cpp's catch for it is gone. `applyInput()`, `processPlayerButtons`, `m_action1Down`, `m_action2Down`, `m_spawnSelection`, `m_controllerDown/Released` and the `DynamicMap*` pointer are gone: `InputHandler` holds no per-player game state. `m_keyMap` is keyed by slot (it was keyed by entity id). `setPointers(players, panel)` lost its `DynamicMap*` parameter and clears the sampler. `takeOverSlotWith*` and `detachController` clear the slot's sampler state so nothing held by the old device leaks to the new one (the only 04b-adjacent change, two lines each).
5. **`GameEngine::stepTick()`**: reads the quit flag, samples, `m_sim.step(inputs)`, then `presentSelections()` copies each slot's selection from the sim to the `InfoPanel` (the held action's item, else `PLAYER_CHARACTER`). `FrameLoop` is untouched: the brief's "wiring into `frame()`" is the existing `step` hook, which is `stepTick()`. `pumpEvents()` still runs every frame and the sample only on a stepped tick.
6. **Tests**: see below. **Docs**: this Outcome, README status, code-facts (§1, §5, §6, §10, §11, §12), tests/README.md (new Input section; the float guard note), `Simulation.h` (input section, hash spec v2), `TickInputs.h` (rules table).

**The apply rules** (the full table is the comment at the top of `src/TickInputs.h`; it reproduces `processPlayerButtons` exactly, apart from the tap rule)

- Idle (no hold): the held direction moves the player (up > down > left > right) and advances its animation, **then** the action rule runs on the same tick: A1 (held or released) begins an A1 hold with selection BLOCK, else A2 (held or released) begins an A2 hold with selection EGG1. A1 beats A2. A hold blocks movement from the next tick.
- A1 hold: direction selects (UP BOMB, DOWN BLOCK, LEFT MINE, RIGHT ROCKET_SPIN; precedence up > down > left > right; sticks after the direction is let go). A1_RELEASED (whether or not A1 is down again) places the selection if the tile has no static entity, then the hold **ends either way** and the selection is BLOCK. A2 pressed, held or released during the hold is ignored.
- A2 hold: direction selects (UP EGG3, DOWN EGG5, LEFT EGG2, RIGHT EGG4); A2_RELEASED places. A1 during the hold is ignored.
- **ACTION1 released while ACTION2 is down, and vice versa (the brief's question): ignored.** The old code only looked for a release inside its own action's hold, and the other action's press was not looked at either. An action still held when the other's hold ends starts its hold on the *next* tick, as before.
- **The tap rule (the one change).** A RELEASED bit for an action that is not down in the sim, when no hold is on, means press-then-release in that tick: the hold begins and ends on the same tick and **places the default** (BLOCK for A1, EGG1 for A2; both verified against the old `processPlayerButtons` and `createEntity`). The movement on that tick still happens. The old behaviour was worse than the brief said: the lost tap left its release *pending*, and the next real hold of that action was **ended on its first tick** by the stale release (an item placed at once, from a key still held). Releases are per tick now and never sticky.
- Tap corner cases, decided conservatively (the same priority the old code gave a press): both actions tapped in one tick -> A1 only; A2 tapped while A1 is held or tapped -> the A2 tap is dropped; A1 tapped while A2 is held -> A1 places, A2's hold begins next tick.

**Sampler encoding (the brief's "define how")**: the held bits are the button states *at the sample*; a released bit means *at least one down-to-up edge since the last sample*, consumed by the sample that reports it. So a tap (pressed and released inside one window) is **RELEASED with HELD clear**; released and pressed again is HELD and RELEASED; two taps in one window are one bit. Direction releases are not recorded (a direction tapped inside one window is not seen, as before; the brief's byte has no bit for it). Events accumulate every frame; the sample is taken only when a tick is stepped, so a tick held back by `canStep()` loses nothing, and on a catch-up frame a release is reported on the first tick only while held buttons are held on every tick.

**Tests** (`bitriot_sim_tests` 258 -> **292**, `bitriot_tests` 306 -> **364**; 34 new in the simulation executable, 24 more in the core one)

- `InputApplyTest` (32, both executables): movement per direction and precedence (including a walled-in winner not letting a loser take over); selection by direction for both actions and its precedence and stickiness; a hold blocks movement; press-then-release places each item (all four A1 items, all five eggs, cost paid, selection reset); the direction held on the release tick counts; no placement on a tile with a static entity (the hold still ends); HELD+RELEASED during a hold places and the next tick starts a new hold (A1 and A2); the other action ignored during a hold (press, hold, release), and the handoff to a still-held action; **the same-tick tap** (A1 places BLOCK, A2 places EGG1, moves on the same tick, no stale release afterwards, static-entity tile, the bounce, the both-actions corners); AI, dead and missing players ignored; slots independent; a new round clears the state; slot range checked; control events refused; `step()` equals `step(TickInputs())`; **the input state is in the hash**; **determinism**: the same seed and the same scripted inputs give the same hash on every one of 900 ticks across runs (2 humans, 2 bots, blocks), and one changed input diverges; **`OnEveryInputTheOldRulesHandledWell...`**: a copy of the old `processPlayerButtons` state machine (sticky releases and all) against the new apply, tick by tick, on 6 x 700 ticks of generated gestures that avoid the situations the old code mishandled: identical states every tick. `OldRulesTest` shows the old bug on that model (the stale release ending a later hold).
- `SimulationTest.APinnedRoundWithScriptedHumanInputKeepsItsHashes`: a fifth golden digest, two humans on scripted random `TickInputs` (taps, bounces, both actions) with two bots, blocks and the spawner, 3001 hashes; equal on GCC and Clang. The four bot-only digests moved (below).
- `InputSamplerTest` (core, 12) + `PlayerInputTest` (1): the encoding (tap = RELEASED alone, release+repress, two taps = one bit, key repeat and stray releases, directions leave no edge, out-of-range slots), edges per sample and never sticky, the local slot mask (non-local slots report nothing and lose their edges), `clearSlot`/`clear`/`peek`. `InputSamplingCadenceTest` (3, `FrameLoop` with a fake clock): **a press-and-release made while `canStep()` held the tick back is reported at the first allowed sample**; a catch-up frame reports a release on its first tick only; a held key is held on every tick of a catch-up frame. `InputHandlerRoutingTest` (8, no SDL events): each keyboard set drives its own slot, every button of a set lands on the right bit, a key tap becomes a RELEASED bit, unmapped keys and unused sets drive nobody, a bot's slot is not sampled, a pad drives its slot and takes the slot from the keyboard, a new round starts with every button up, and keys -> `sampleTick()` -> `sim().step()` -> a moved player and a placed egg.
- Changed existing tests, no assertion loosened: `PinnedRoundsKeepTheirHashes` (new digests, below), `TheHashSchemaIsPinned` (three new field lines), `FixedPointSourceGuardTest.NoFloatingPointInSimulationSources` / `TheGuardReadsTheSimulationAndNotTheRenderer` (03b's follow-up: `coreFilesThatStillMutateTheSimulation()` is removed from `tests/support/SimSources.h`; the guard reads the simulation only, and now asserts that `TickInputs.h` is read and that `InputHandler`, `InputSampler` and `GameEngine` are **not** in the simulation's list). No test was skipped or quarantined.

**What was shown to fail without the change.** The new API does not exist before this branch, so the new tests cannot be compiled against the old code (and the old `InputHandler` needs SDL singletons and an `InfoPanel`). Instead 30 mutations were applied to the committed code one at a time, the suites rebuilt and run (both executables), the file restored. All 30 are killed (one survived the first time and got a test: `ReleasingWhileStillHoldingTheButtonStillPlaces` for A1).

| Mutation | Result |
|---|---|
| **The old tap behaviour**: idle A1 begins a hold only when HELD (a release alone ignored) | 6 fail: `ASameTickTapOfActionOnePlacesTheDefaultBlock`, `ATapLeavesNoStaleReleaseBehindToEndTheNextHold`, `ATapMovesThePlayerOnTheSameTickLikeAnyPress`, `ATapOnATileWithAStaticEntityPlacesNothing`, `WhenBothActionsAreInvolved...`, and the scripted golden |
| The same for A2 | `ASameTickTapOfActionTwoPlacesEggOne`, the scripted golden, and (core) `KeysBecomeMovementAndAPlacedItemThroughTheSimulation` |
| Tap begins the hold but does not place | 7 fail |
| Idle precedence DOWN before UP; A1 UP selects MINE; A2 LEFT selects EGG4; A2 press selects EGG2 | `DirectionPrecedence...`; 8 selection/placement tests plus the old-rules oracle; 4; 8 |
| A1 release honoured only if A1 is no longer held | survived, then `ReleasingWhileStillHoldingTheButtonStillPlaces` (A1 half added) fails it |
| An A1 release ends an A2 hold | `TheOtherActionIsIgnoredWhileAHoldIsOn` + golden |
| Placement ignores a static entity on the tile | 4 fail |
| AI players / dead players driven by input | `InputForAnAIPlayerOrAnEmptySlotIsIgnored`; `InputForADeadPlayerIsIgnored` + golden |
| A hold does not block movement | `HoldingAnActionBlocksMovement`, `MovementIsAppliedBefore...`, golden |
| Input state not reset by `start()`; left out of the hash; selection left out of the hash | 7 fail; `ThePerSlotInputStateIsPartOfTheHash`, `TheHashSchemaIsPinned`, both golden tests (4 each) |
| Control events silently dropped | `ControlEventsAreNotSupportedYet...` |
| Slot index off by one in `step()` | 21 fail |
| **Sticky releases** (`sample()` keeps the edges, the old behaviour) | 9 fail in the core executable |
| A tap lost (no edge for press+release between samples); a tap reported as held; a stray release records an edge; direction release on the wrong bit | 12, 13, 1, 7 fail |
| Non-local slots keep their edges / are reported anyway | 2 fail each |
| Keyboard ignores the attached pad; bots sampled; keys routed to slot + 1; round start does not clear the sampler; pad button mapped to the wrong bit | 1, 1, 4, 1, 1 fail |

**Golden digests and the hash**

- **The four bot-only digests moved, and `HASH_SCHEMA` gained three lines, because the state hash gained the per-slot input section (version 2). The simulation's play did not change**: with the new section left out of `hashInto` (and the version back at 1), all 13,211 per-tick hashes of the four pinned rounds are byte-identical to the dump from `claude` (`cmp` on `BITRIOT_HASH_DUMP=all`), and the rounds have the same tick counts (4302, 6552, 1210, 1147) with the new section. The new digests: `0x494d4e50730d8936`, `0x5b6ba5e67602feaa`, `0xd8ba8146dc790f7d`, `0xda5b6510b076c259`; the fifth (scripted humans) is `0xf493bd22680761b3`. The commit message says so.
- GCC and Clang agree on every tick of all five rounds (16,212 hashes; the `BITRIOT_HASH_DUMP=all` dumps are byte-identical, and the GCC Debug build's too).

**Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, from the build directory, each against a clean build of its own tree)

- before (`claude`, Release): `bitriot_tests` (306) **0 failing seeds**, `bitriot_sim_tests` (258) **0 failing seeds**.
- after (Release): `bitriot_tests` (364) **0 failing seeds**, `bitriot_sim_tests` (292) **0 failing seeds** (all 200 seeds pass every test). The new tests start their rounds through the harness, so the sweep varies them; the scripted-input and gesture tests pin their own seeds on purpose.

**GCC and Clang**: GCC 13.3 Release and Debug, Clang 18.1 Release: 364 / 292 pass on each, and the Debug build's per-tick hashes of the five pinned rounds equal the Release build's and Clang's byte for byte.

**Manual check of the real engine** (`GameEngine` cannot run headlessly; this is what the cloud container can show). A scratch copy of the tree (not committed) with an env-var hook that starts all-bot-plus-one-human rounds from the menu state and pushes real SDL key events (`SDL_PushEvent`) at chosen ticks, run under `xvfb-run` with `SDL_AUDIODRIVER=dummy`; it logged the `TickInputs` bits and the state hash of every tick.
- Holding `d` moved the human right (`PlayerInput` bit 3 from tick 4); **a key down and up pushed in the same frame reached the sim as `0x40` (ACTION1_RELEASED alone) and placed a block**; `a` held after it walked the player off the block; LShift held then `w` selected EGG1 then EGG3 (HUD calls `PANEL ... selection 1`, `3`, then back to the no-selection marker on release), and its release (`0x80`) placed an EGG3; LCtrl + `a` selected a MINE (HUD selection 9) and its release (`0x40`) came through on tick 96. ESC pushed at tick 120 was seen on tick 121, the round ended, and the next round started clean: its 121 per-tick hashes equal the first round's.
- **The recorded inputs of that round, replayed through `Simulation::step(inputs)` in the harness, give the same state hash on all 121 ticks** (0 mismatches). So the engine's sampling and `stepTick()` wiring feed the sim exactly what a test or a replay would.
- Not exercised: a real keyboard and pad, rumble, the join selector and hotplug, key repeat from a real keyboard, the Options flow. Those are the play-test.

**Deviations from the brief**

- `step()` with no argument is kept next to `step(const TickInputs&)` (the brief only names the latter) so bot-only code and the existing tests (which call `sim().step()` throughout) do not have to build an empty `TickInputs`.
- `ControlEvent` is an empty placeholder, and a non-empty `control` makes `step()` throw until 04b. The brief said the vector "stays empty until 04b"; this makes that enforced.
- `InputSampler` is in `bitriot_core`, not `bitriot_sim` (it is the local half; a headless binary does not need it). Its tests are therefore in the core executable only. `TickInputs.h` is in the simulation.
- The sampler's release encoding for a tap is RELEASED-with-HELD-clear, and the sim decides tap vs end-of-hold from its own state (the brief left both to me).
- A quit tick now applies that tick's input (the old exception skipped it). ESC is not a sim input, and the round is being abandoned on this machine anyway.
- The `InfoPanel` selection now updates on the press tick; before it appeared one tick later (33 ms), because the press tick set the hold but called no `setSelection`.
- `m_keyMap` is keyed by slot (was entity id), so the (unused) `getPlayerIdFor*Button/Key` helpers now return slots. `GameRunningSDLInput` still maintains its `keysCurrentlyDown` / `keysRecentlyReleased` sets for the `MickBaseInput` interface; gameplay no longer reads them.
- `InputException.h` and its catch in main.cpp are removed (nothing threw it any more); `throw` sites in src: 17 (code-facts and brief 09 updated).
- `coreFilesThatStillMutateTheSimulation()` is removed, as 03b's follow-up asked. **Caveat:** `InputHandler`'s join paths (`takeOverSlotWith*`, `detachController`, run from `pumpEvents()`) still write `m_controlledByAI` and `attachedController`, which are sim state, from outside `step()`. That is 04b's. The float text guard no longer reads them; it never found anything in them.
- Brief corrections: none of its facts about the code were wrong. Its "a release places the selection, if the tile has no static entity, and resets to BLOCK" omits that the hold ends (and resets) *whether or not* the tile is free; written into the table. Its "wiring into `frame()`" is `GameEngine::stepTick()` (the `step` hook), not `FrameLoop`. The brief understates the old tap bug (the stale release also ends a later hold at once).

**Follow-ups**

- **04b (control events, rumble):**
  - `ControlEvent` is an empty struct; define it, apply the events in `Simulation::step` before the inputs (the slot is there: `TickInputs::control`), and remove the `step()` throw.
  - The join selector and hotplug still mutate `m_controlledByAI` and `attachedController` directly during `pumpEvents()` and clear the slot's sampler state. They must become control events applied at a tick. Today a takeover or unplug **does not reset** the sim's `SlotInputState` (a human unplugged mid-hold keeps `action1Down` and the hash shows it; a later takeover resumes it, as the old maps did). Decide whether a control event resets it (it changes the hash, so decide once).
  - `keyboardEvent()` consults `p_players[slot]` (`isControlledByAI()`, `attachedController`) at event time: that is local device routing and may stay, but a keyboard player turned into an AI by a control event must stop being routed (it does, live).
  - Rumble is untouched (`onCollision` still calls `RumbleSink`); `attachedController` is still on the entity.
  - `InfoPanel::setJoinIndicator` calls remain in `InputHandler` (join UI).
- **05 (replay):** a tick's input is `TickInputs`: six `PlayerInput` bytes and the control events. Record `TickInputs` per stepped tick (plus `MatchConfig`) and replay with `step(inputs)`; the headless binary links `bitriot_sim` only and needs neither the sampler nor `InputHandler`. `tests/support/ScriptedInput.h` (`randomInputs`, `gestureInputs`) is a template for input generators. The determinism test and the scripted golden round are the proof that a recorded script reproduces. `SimulationTest.APinnedRoundWithScriptedHumanInputKeepsItsHashes` should become part of 05b's gate (`BITRIOT_HASH_DUMP=all` now also prints its 3001 hashes).
- **05b:** the dump now has five rounds: filter `SimulationTest.*Pinned*`, not `PinnedRounds*`.
- **07 (lockstep):**
  - Build the local slot mask from the local players and pass it to `InputSampler::sampleTick(mask)` (`InputHandler::sampleTick()` currently uses every human slot); fill the remote slots' bytes from the network before `step()`. Remote humans must not be given keyboard sets in `setPointers` (code-facts §12).
  - **Sampling cadence needs a decision.** 04a samples when a tick is *stepped*, which is right locally. With input delay D, a peer samples the tick it produces and sends it, and a stalled `canStep()` must not drop edges: the sampler keeps them until the next sample, so a stall merges taps (two taps in one window are one RELEASED bit). If 07 samples once per produced tick (not per stepped tick), keep the sampler where it is and call `sampleTick` from the production hook.
  - The tap rule reads the sim's per-slot state, which is hashed, so peers cannot disagree on it.
  - Compare `stateHash()` as before; the hash has the input state in it now (version 2), so a peer on an old build disagrees at tick 0.
- `PROMPT.md` §0 and the golden-digest lesson there say "four pinned rounds"; there are five.

**Play-test** (pending; needs a display and ideally a keyboard and a pad). The commands are untested on Arch (the container is Ubuntu). RmlUi 4.3 is not packaged for Arch, so it is built from source as `scripts/cloud-setup.sh` does, with the two flags Arch's newer tools need (CMake 4.x needs a policy minimum, GCC 15 no longer pulls in `<cstdint>`).

```bash
# one-time: dependencies
sudo pacman -S --needed base-devel cmake git sdl2 sdl2_mixer sdl2_ttf sdl2_image freetype2 mesa
git clone --depth 1 --branch 4.3 https://github.com/mikke89/RmlUi.git /tmp/RmlUi
cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_SAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_LUA_BINDINGS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"
cmake --build /tmp/RmlUi/build -j"$(nproc)" && sudo cmake --install /tmp/RmlUi/build && sudo ldconfig

# AFTER: this branch (it is on origin; use `claude` instead once it is merged)
git clone --branch netplay/04a-input-sampling https://github.com/VenKamikaze/BitRiot.git ~/bitriot-after
cmake -S ~/bitriot-after -B ~/bitriot-after/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-after/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-after/build && ./BitRiot)

# BEFORE (for comparison): the commit before this branch, e474cf3 (`claude` before 04a)
git clone https://github.com/VenKamikaze/BitRiot.git ~/bitriot-before
git -C ~/bitriot-before checkout e474cf3
cmake -S ~/bitriot-before -B ~/bitriot-before/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-before/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-before/build && ./BitRiot)
```

Play with at least one human on the keyboard (and a second keyboard set or a pad if you have them) against bots, spawner on. Nothing should feel different except item 3. Check 03a's and 03b's lists quickly (rounds, countdown, scoreboard, ESC, play again, map-size change, takeover, speed, stall, rumble), then:

1. **Movement.** Walk in all four directions on each keyboard set you have (WASD, arrows, keypad 8/5/4/6, IJKL) and on a pad (d-pad). Pressing two directions at once picks up > down > left > right, as before.
2. **Placing.** Hold ACTION1 (LCtrl on set 1, A/X on a pad) and tap a direction: the HUD icon beside your portrait shows block/bomb/mine/rocket; release places it. Hold ACTION2 (LShift, B/Y) for an egg: nothing = egg 1, up/left/right/down pick the others. You should not move while an action is held. Place something on your own tile twice: the second time nothing is laid (a static thing is already there) but the hold still ends.
3. **The tap (the one change).** A tap shorter than one tick (33 ms) is rare by hand, so this may take some mashing; if you cannot make it happen, that is fine. Tap each action as fast as you can, without holding. NOW: a block or an egg 1 is placed. BEFORE: nothing happened, and the *next* time you held that action it was placed at once, a tick after you pressed it, while you were still holding. Check that this no longer happens: tap A1, wait, then hold A1 and push a direction: the selection should show and nothing is placed until you release. (The same case is the common one over a network, which is why the rule exists.)
4. **Both actions.** Hold ACTION1, press ACTION2 as well: nothing changes. Release ACTION1 while still holding ACTION2: the block is placed and the egg selection starts a moment later.
5. **Two players at once** on two keyboard sets (or keyboard + pad), both holding actions and moving: no cross-talk, no stuck key.
6. **ESC** mid-round, **a slow frame** (drag the window) and **a pad unplug** mid-hold: the round ends / the game catches up / the player reverts to a bot, no placed item appears from a key you were holding when the pad went away.

Record the result here when it comes back.

**Play-test result:** *(pending)*
