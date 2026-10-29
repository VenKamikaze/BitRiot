# 04b — Control events, rumble and UI plumbing

- **Repo:** BitRiot
- **Branch:** `netplay/04b-control-events`
- **Depends on:** 04a
- **Status:** done (play-test pending)

## Goal

Every change of who controls a slot (AI to human, or human back to AI) becomes an explicit `ControlEvent` that the sim applies on a specific tick. No more flipping `m_controlledByAI` from input code.

Also, the sim no longer drives local devices or UI directly:

- **Rumble:** the sim records that slot N was hit this tick, and the local layer rumbles the device bound to that slot.
- **InfoPanel selection:** a read-only view of the sim's per-slot selection.

## Read first

- [`research/code-facts.md`](research/code-facts.md) §6: the Phase 2.7 join selector, `detachController`, hotplug and rumble from `onCollision`.
- `src/InputHandler.cpp` (`processJoin*`, `openJoin`/`navigateJoin`/`confirmJoin`, `takeOverSlotWith*`, `detachController`), `src/PlayerCharacterEntity.{h,cpp}` (`m_controlledByAI` and the rumble in `onCollision`), and `src/InfoPanel.*`.
- The Outcome of brief 04a.

## Design

- **`ControlEvent`:** `{slot, kind}`. `kind` is one of:
  - `HUMAN_TAKES_OVER`
  - `RETURN_TO_AI`
  - `LEAVE`, reserved for netplay; it can alias `RETURN_TO_AI` for now.

  Events are carried in `TickInputs` and applied at the start of the tick.
- **Who produces them** (all local input-layer code):
  - the join selector (confirming a takeover)
  - keyboard takeover
  - controller unplug (`detachController` when the player has no keymap)

  The local layer also updates its own device-to-slot mapping and local slot mask at the same moment.
- **`m_controlledByAI` becomes private.** It changes only when the sim applies an event.
- **AI goal and instruction lists on a switch.** Today they persist across switches. **Clear them on both transitions.** That is deterministic and avoids a resumed bot acting on a stale plan. Note it in the Outcome as a behaviour change.
- **Rumble:** the sim records `hitThisTick[slot]`, and after the step the local layer rumbles the device for that slot, if it has one. Don't rumble from inside `onCollision`.
- **InfoPanel:** reads the per-slot selection from the sim. No `p_infoPanel` calls from apply code.
- **Hash:** control state is already in `stateHash()` through `m_controlledByAI` (03a). Make sure any new per-slot state is hashed too.

## Out of scope

- Networking the events (07).
- Hot-join (13).

## Tests

- `HUMAN_TAKES_OVER` stops AI decisions for that slot on exactly that tick, and scripted inputs then drive it.
- `RETURN_TO_AI` resumes AI on that tick, with cleared goals.
- An unplug produces `RETURN_TO_AI` for a controller-only slot, and nothing for a slot that has a keymap.
- `hitThisTick` is set when a player is damaged, and rumble isn't called from sim code. Check with a fake device layer.
- **Determinism:** a scripted sequence with takeovers gives an identical hash per tick across runs.
- Run a 200-seed sweep.

## Acceptance criteria

- `m_controlledByAI` is changed only through events.
- No device or UI calls remain in sim code.
- The join, takeover and unplug flows still work locally. This needs a play-test, which may be pending at merge.

## Outcome

**Status: done, play-test pending.** Who controls a slot is now a `ControlEvent` that `Simulation::step` applies at the start of a tick, and nothing else can change it: `PlayerCharacterEntity::m_controlledByAI` is private (a friend, `Simulation`, calls `applyControl`). The simulation touches no device and no UI any more: a hit is recorded (`Simulation::hitThisTick(slot)`) and the input layer buzzes the pad after the step; `InfoPanel` reads the sim's per-slot selection. Everything automated passes on GCC and Clang, the **five golden digests are byte-identical** (nothing hashed was added), and the real engine (SDL events, pad join, keyboard join, device removal) was run under Xvfb and its recorded `TickInputs`, control events included, replay in the harness to the same hash on all 121 ticks. The user should play it once (steps below), mostly for the join/unplug flows and for rumble, which nothing here could exercise on a real pad.

**What landed**

1. **`ControlEvent`** (`src/TickInputs.h`, in `bitriot_sim`): `{int slot; Kind kind}` with `HUMAN_TAKES_OVER` (0), `RETURN_TO_AI` (1) and `LEAVE` (2, which does exactly what `RETURN_TO_AI` does today). `TickInputs::control` carries them. The rules are written on the type:
   - applied in order at the start of the tick (tick++, last tick's hit flags forgotten, the events, then the inputs), so a takeover tick's input drives the new human and a return tick's input is ignored;
   - **both transitions clear the player's AI goal and instruction lists and reset the slot's `SlotInputState`**;
   - an event that changes nothing, or one for a dead player or a missing one, does nothing and resets nothing (so a repeated event is harmless);
   - a slot outside the round's players, or an unknown kind, throws (`std::out_of_range` / `std::invalid_argument`) **before anything of the tick happens**; the step's old `logic_error` for any event is gone.
2. **`m_controlledByAI` private**, changed only by `PlayerCharacterEntity::applyControl(toAI)`, which `Simulation` alone can call. The constructor still sets it from the creation flag. Tests that had written the field now use events or the config (`setPlayerAI` before `startRound()`).
3. **Rumble.** `onCollision` sets a private `m_hitThisTick`; `Simulation::step` clears the flags at its start and collects them at its end into `hitThisTick(slot)` (valid between steps; false after `start()`; bots are recorded too, the sim knows nothing of devices). `GameEngine::stepTick` calls `InputHandler::rumbleForHits(sim)` after the step: for every hit slot that is a human with a pad it calls `ControllerRumble::rumble(pad, 0.5f, 200)` (SDL haptics by default, `setRumble` replaces it, which is the fake device layer of the tests). **`RumbleSink`, `MickSDLRumbleSink`, `RumbleSink.cpp` and the entity's `attachedController` are gone.** `SimLibraryTest`'s source scan now also refuses a `rumble` or `haptic` identifier and an `InputSampler` include in the simulation.
4. **`InputHandler` owns the device side.** `m_slotController[slot]` (the pad that drives each slot, seeded from the menu's `GameSettings` choice in `setPointers`), `m_localSlots` (the local slot mask, passed to the sampler, used to route keys and to hide a slot another device has just claimed from the join selector) and `m_pendingControl`. The join selector's confirm (`takeOverSlotWithController` / `WithKeyboard`) and `detachController` update the mapping, the mask and the slot's sampler state **when the event is produced**, and queue a `HUMAN_TAKES_OVER` or (unplug of a live player with no keymap) `RETURN_TO_AI`; `sampleTick()` hands the queued events out in `TickInputs::control`, once. The selector's `InfoPanel::setJoinIndicator` calls stay (join UI) and are skipped when there is no panel (tests).
5. **`InfoPanel`** has no `setSelection`/`clearSelections` and no `playerSelection` array: it is given the `Simulation` once (`setSimulation`) and reads `Simulation::heldSelection(slot)` (the held action's item, else `PLAYER_CHARACTER`) when it draws. `GameEngine::presentSelections()` is gone.
6. **Tests**: below. **Docs**: this Outcome, README status, code-facts (§1, §4, §6, §7, §10, §12), `tests/README.md` (Control events paragraph, the sixth pinned round, the guards), `Simulation.h` (step order, `hitThisTick`, `heldSelection`, the not-hashed list), `TickInputs.h` (`ControlEvent`).

**Tests** (`bitriot_sim_tests` 292 -> **315**, `bitriot_tests` 364 -> **411**: 26 `ControlEventTest` and a sixth golden round in both, 24 `InputControlTest` in the core one; the placeholder `ControlEventsAreNotSupportedYet...` and the three `RumbleSinkTest`s are removed because the code they tested is gone, and `ALocalControllerAssignmentIsNotHashed` became `TheHitFlagIsAnOutputOfTheTickAndNotHashed`)

- `ControlEventTest` (both executables, scripted `TickInputs` only): the bot's `m_aiFrame` (a protected-member accessor) **stops on exactly the takeover tick** and never moves again, and **resumes on exactly the return tick**; a takeover tick's input moves the player that tick, a bot's slot ignores input, a return tick's input is ignored; both transitions clear a planted sentinel goal and instruction (and a taken-over bot's real plan); both reset the hold and the selection (a hold unplugged mid-way is not inherited by the next device); events that change nothing leave the hash identical to a run without them, tick for tick (a human taken over, a bot returned, `LEAVE` on a bot), and so do events for a dead player (whose own record is untouched too); `LEAVE` equals `RETURN_TO_AI` hash for hash; two events for one slot end in the last, events for several slots all apply; a bad slot (high, negative, `MAX_PLAYERS`) or kind is refused with the tick and hash unchanged **and an earlier valid event of the same tick not applied**; events before `start()` are refused like any step; **determinism**: a 700-tick scripted sequence of takeovers, returns and a leave gives the same hash on every tick of two runs, and leaving out any one of its nine events changes the hashes from exactly that tick on; `hitThisTick` follows damage (set for the hurt slot only, cleared by the next step, false after `start()`, recorded for a bot and for the hit that kills, slots range-checked); `heldSelection` follows the hold and is emptied by a switch.
- `SimulationTest.APinnedRoundWithControlEventsKeepsItsHashes`: a **sixth golden digest** `0x7f1985741702efdd` (4 players, humans in slots 0 and 1, random scripted input on all four, ten events: takeovers, returns, a `LEAVE`, a slot switching twice; the round ends at tick 1671). Equal on GCC, Clang and the Debug build.
- `InputControlTest` (core only; `InputHandler`'s public entry points driven by hand, a recording `ControllerRumble`): a controller join opens on START and confirms on the second START and queues `HUMAN_TAKES_OVER` while the entity stays a bot until `sim().step()`; the slot is local and its pad mapped from that moment, and the takeover tick carries the new pad's input (the player moves on that tick); a keyboard join (set 1's action key twice) likewise, with the set routed from then on; cancelling produces nothing; navigation reaches another bot; **a slot a first joiner has claimed but the sim has not heard of yet is not offered to the next joiner** (by pad, and by keyboard set); no bot left or a bot that died before the confirm produces nothing; an unplug of a controller-only slot queues `RETURN_TO_AI`, the entity changes on the step, and the slot can be joined again; **an unplug of a slot that also has keys queues nothing and the slot stays local**; an unplug of an unattached pad, of `-1` or of a dead player's pad queues nothing; keys do not drive a slot with a pad, and do once the pad is gone; a gone pad leaves no button down; **rumble**: the sim rumbles nothing, `rumbleForHits` buzzes once per hit pad human per tick with `{pad, 0.5, 200}`, never for a keyboard human, a bot, a bot whose pad is still mapped (a slot returned by an event this layer did not make), a slot whose pad was unplugged, or a quiet tick; a taken-over slot rumbles its new pad; a new round forgets pending events and mid-round mappings and re-seeds the menu's pad.

**What was shown to fail without the change.** The new API does not exist before this branch (`ControlEvent` has no fields, `hitThisTick`, `rumbleForHits` and `applyControl` do not exist), so the new tests cannot be compiled against the old code. As in 04a, mutations were applied to the new code one at a time, both executables rebuilt and run, the file restored. **38 mutations, 38 killed** (four survived the first run - events for dead players, a keyboard-claimed slot offered to a pad, rumble for a bot with a mapped pad, keys driving a slot with a pad - and each got a test; the rows say so).

| Mutation | Killed by |
|---|---|
| Control events silently dropped | 17 sim tests (26 core), incl. the takeover/return/selection/determinism tests |
| Events applied after the inputs | `ATakenOverPlayerMovesByTheInputOfTheTickOfTheEvent`, the sixth golden, `TheTakeoverTickCarriesTheNewDevicesInput` |
| Takeover treated as a return; `LEAVE` as a takeover | 14 / 4 |
| Slot input state not reset by a switch | `BothTransitionsReset...`, `ATransitionResetsAHold...`, `ASwitchEmptiesTheHeldSelectionToo`, the sixth golden |
| Events for dead players applied (**survived**, then `ADeadPlayersEventsAreIgnored` got the roster check) | killed by it |
| Events not validated; negative slot accepted; unknown kind accepted | 2 / 1 / 1 |
| Hit flags not cleared at step start; hits never collected; `start()` does not clear them; hit recorded for the wrong slot; `onCollision` does not record | 1 / 4 / 1 / 4 / 4 |
| `heldSelection` ignores whether a hold is on | 2 |
| A no-op event still clears the plans | `AnEventThatChangesNothingDoesNothing` |
| Plans not cleared; cleared only on return; cleared only on takeover | 4 / 2 / 2 |
| Controller takeover queues no event; keyboard takeover queues none | 9 / 1 (core) |
| Unplug queues an event for a slot with keys; leaves the slot local; keeps the pad mapped; keeps the sampler state; reaches a dead player | 1 / 2 / 3 / 1 / 1 |
| Takeover does not make the slot local (controller; keyboard) | 3 / 1 |
| A claimed slot is still offered to the next joiner (**survived**, then `ASlotTakenByAKeyboardSet...`) | killed |
| `sampleTick` hands out no events; repeats them every sample | 13 / 5 |
| Rumble for a bot (**survived**, then `ABotWhosePadIsStillMapped...`); for a slot with no pad; without a hit; wrong duration | killed / 1 / 2 / 1 |
| A new round keeps pending events; `setPointers` does not seed the pads | 1 / 11 |
| Keys drive a slot that has a pad (**survived**, then `KeysDoNotDriveASlotThatHasAPad...`) | killed |
| A `rumble...` identifier added to `Simulation.cpp` | `SimLibraryTest.TheSimulationSourcesNameNoSdlRmlUiOrOpenGL` |

**Not covered by an automated test (the engine glue).** `GameEngine::stepTick()` calling `rumbleForHits`, `GameEngine::resetGame()` calling `InfoPanel::setSimulation`, `InfoPanel`'s read of `heldSelection`, and `GameRunningSDLInput` forwarding SDL events to the join/unplug entry points need SDL and a window. A mutation of those lines is not caught by the suite; the Xvfb check below covers the join and unplug paths and the tick wiring, not rumble or the panel's drawing.

**Golden digests and the hash: unchanged.** All five pinned digests pass untouched, and the per-tick dump (`BITRIOT_HASH_DUMP=all`, 16,217 hashes) of the five is **byte-identical to the dump from `claude` (d899b57)** (`cmp`). Nothing hashed was added: the control flag (`player.controlledByAI`), the AI goal and instruction lists and the per-slot input state were already in the hash, and the new hit flag is deliberately not (see decisions). No existing round has a control event, so the cleared-goals rule cannot reach them. `STATE_HASH_VERSION` stays 2; the schema list is untouched. The sixth round adds 1,672 hashes; GCC Release, GCC Debug and Clang agree on all 17,889 (`cmp` of the three dumps).

**Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, from `build/`, each against a clean build of its own tree)

- before (`claude` d899b57, Release, a separate worktree): `bitriot_tests` (364) **0 failing seeds**, `bitriot_sim_tests` (292) **0 failing seeds**.
- after (Release): `bitriot_tests` (411) **0 failing seeds**, `bitriot_sim_tests` (315) **0 failing seeds**.

**GCC and Clang**: GCC 13.3 Release and Debug, Clang 18.1 Release: 411 / 315 pass on each. A `-DBITRIOT_BUILD_GAME=OFF` configure (no SDL, RmlUi or OpenGL looked for) still builds `bitriot_sim_tests`; 315 pass.

**Manual check of the real engine** (`GameEngine` cannot run headlessly; this is what the cloud container can show). A scratch copy of the tree (not committed) with an env-var hook that pushes real SDL events (`SDL_PushEvent`: keys, `SDL_CONTROLLERBUTTONDOWN/UP`, `SDL_CONTROLLERDEVICEREMOVED`) at chosen ticks and logs each tick's `TickInputs` and state hash, run under `xvfb-run` with `SDL_AUDIODRIVER=dummy`. Three players, slot 0 a human on pad 5 (set up through `GameSettings`), two bots, seed 4242:
- pad 9 START (tick 10) opened the selector, START again (tick 14) confirmed: **tick 15 carried `HUMAN_TAKES_OVER` for slot 1**, the player became a human on that tick, and pad 9's d-pad then drove it (bit `02`, DOWN);
- RCTRL twice (keyboard set 1, ticks 70 and 74): **tick 75 carried `HUMAN_TAKES_OVER` for slot 2**; the arrow-left pushed at tick 80 reached slot 2 as bit `04` from the next frame;
- `SDL_CONTROLLERDEVICEREMOVED` for pad 5 (tick 100): **tick 101 carried `RETURN_TO_AI` for slot 0**, its buttons were dropped from that tick on (`00`), it was a bot from that tick (`ai=100`), and its held selection (ROCKET_SPIN from A + d-pad right) was cleared;
- slot 0 held A + right with the pad: `heldSelection` showed ROCKET_SPIN (10);
- **the recorded `TickInputs` of all 121 ticks, replayed through `Simulation::step(inputs)` in the harness, give the same state hash on every tick (0 mismatches)**, so the engine's join, unplug and tick wiring feed the sim exactly what a test or a replay would.
- Not exercised: a real pad, rumble (no hit happened in that script and there is no haptic device), the panel's drawing of the selection and join indicators, the Options flow. Those are the play-test.

**Behaviour changes** (all deliberate)

1. **A bot's AI goals and instructions are cleared when a human takes it over, and again when it goes back to the AI** (the brief's decision). Before, they persisted across a switch, so a bot handed back resumed a plan made before a human had played it. After a return the bot re-plans from nothing (`determineGoals` runs on that tick). A taken-over bot's real plan is gone too, which nobody could see. **The slot's hold and selection are also reset on both transitions** (04a's follow-up, decided here): an unplugged human mid-hold no longer leaves a phantom hold for the next device.
2. **A takeover or an unplug takes effect at the start of the next stepped tick**, not in the middle of the event pump. Locally that is the same frame (pump, then step), so it cannot be seen; the sim just never changes between ticks now.
3. **Rumble is once per hit tick per player**, from the local layer after the step. A player hit by two things in one tick used to restart the 200 ms rumble twice in a row; it is one 0.5 / 200 ms rumble now, which is the same buzz. Who rumbles is unchanged (a human with a pad), and so are the strength and duration.
4. The join selector never offers a slot that another device has just claimed but the simulation has not yet applied (it could not happen before, because the claim was immediate).

**Deviations from the brief and decisions** (the ambiguous ones took the most conservative option)

- **`hitThisTick` is not hashed.** The brief says to hash "any new per-slot state". It is an output of one tick, cleared before the next and never read by the sim, and it follows from health, which is hashed in the same tick, so a desync in hits shows as a desync in health. Hashing it would move every golden digest and need version 3 for nothing. If a reviewer disagrees: add a section 6c to `hashInto`, bump `STATE_HASH_VERSION`, update the schema test and the digests.
- **Events for a dead player or a missing one are ignored, not refused.** In netplay an event can arrive on the tick its target dies; ignoring is deterministic and harmless, and refusing would let a race crash a peer. A slot outside the round is refused (that is a bug or an attack, not a race).
- **A no-op event resets nothing**: so a protocol that repeats events (D7) is safe.
- `RumbleSink` and `MickSDLRumbleSink` and `attachedController` were removed, which 03b's follow-up said could go once `hitThisTick` existed. `ControllerRumble` (in `InputHandler.h`) takes the SDL float strength; the integer-strength seam is no longer needed because the simulation passes nothing.
- The InfoPanel is given a `Simulation*` (a pull) rather than being pushed `setSelection` calls; the brief's "a read-only view of the sim's per-slot selection". `Simulation::heldSelection()` holds the "no selection is `PLAYER_CHARACTER`" convention so it is testable without a surface.
- The local slot mask is now this layer's own (`m_localSlots`), not derived from the entities' AI flags, so a takeover's first sample is not lost between production and application.
- Brief corrections: none of its facts about the code were wrong. Code-facts' §6 said the takeover wrote `m_controlledByAI=false` at PlayerCharacterEntity.h:69 and `attachedController` at PlayerCharacterEntity.cpp:121-125; both are gone and §6 describes the new flow.

**Follow-ups**

- **05 (replay):** `TickInputs::control` is part of a tick's input, so a recording is `MatchConfig` plus, per stepped tick, six `PlayerInput` bytes and zero or more `{slot, kind}` events (kind values 0, 1, 2 are stable: they are in the record format now). Replay is `step(inputs)` and needs nothing from the input layer. `SimulationTest.APinnedRoundWithControlEventsKeepsItsHashes` is a ready fixture for a record/replay test (events and scripted inputs, 1671 ticks). `BITRIOT_HASH_DUMP=all` now prints six rounds; the filter for 05b is `SimulationTest.*Pinned*` (the dump labels the new one "control events").
- **07 (lockstep):**
  - Events and the inputs of the tick they were sampled on travel together in `TickInputs` (`InputHandler::sampleTick()` returns both); keep them together through the input delay so a takeover and its first inputs apply on the same tick.
  - **The host must validate events from peers before broadcasting.** `step()` throws on a slot outside the round or an unknown kind, so one bad event from one peer would crash every peer. Check the slot against the roster and that the sender owns it; a no-op or a dead target is harmless and does not need filtering.
  - A remote human's event arrives from the network: it must **not** touch `m_slotController`, `m_localSlots` or `m_keyMap`. `isLocalSlot(slot)` is the question to ask; give remote humans no keyboard set in `setPointers` (code-facts §12).
  - The local view changes at production and the sim at application, up to the input delay D apart. In that window a local join is "mine" locally and a bot in the sim; its inputs are sent and ignored until the event applies. Nothing is lost.
  - Repeated events are safe (no-ops), which suits D7's repeat-everything packets.
- **13 (disconnects, hot-join):** `LEAVE` is `RETURN_TO_AI` today; a peer that drops is a `LEAVE` event for each of its slots, applied on the agreed tick (the bot starts from cleared plans, the same on every peer). A hot-joiner takes over a bot with `HUMAN_TAKES_OVER` the same way, but it also needs the world: the roster and `MatchConfig` are fixed at `start()`.
- **Untested glue** (above): `stepTick()`'s `rumbleForHits` call, `setSimulation` and the panel's draw. A cheap guard if wanted: a `FrameLoopTest`-style test cannot reach them without SDL; a mutation there is only caught by the play-test.
- `PROMPT.md` §0 and its golden-digest lesson say "four pinned rounds"; there are six now (04a already made it five).

**Play-test** (pending; needs a display and ideally a keyboard and a pad). The commands are untested on Arch (the container is Ubuntu). RmlUi 4.3 is not packaged for Arch, so it is built from source as `scripts/cloud-setup.sh` does, with the two flags Arch's newer tools need (CMake 4.x needs a policy minimum, GCC 15 no longer pulls in `<cstdint>`).

```bash
# one-time: dependencies (skip what you already have from 04a)
sudo pacman -S --needed base-devel cmake git sdl2 sdl2_mixer sdl2_ttf sdl2_image freetype2 mesa
git clone --depth 1 --branch 4.3 https://github.com/mikke89/RmlUi.git /tmp/RmlUi
cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_SAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_LUA_BINDINGS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"
cmake --build /tmp/RmlUi/build -j"$(nproc)" && sudo cmake --install /tmp/RmlUi/build && sudo ldconfig

# AFTER: this branch (it is on origin; use `claude` instead once it is merged)
git clone --branch netplay/04b-control-events https://github.com/VenKamikaze/BitRiot.git ~/bitriot-after
cmake -S ~/bitriot-after -B ~/bitriot-after/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-after/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-after/build && ./BitRiot)

# BEFORE (for comparison): `claude` before this brief, d899b57 (04a merged)
git clone https://github.com/VenKamikaze/BitRiot.git ~/bitriot-before
git -C ~/bitriot-before checkout d899b57
cmake -S ~/bitriot-before -B ~/bitriot-before/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-before/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-before/build && ./BitRiot)
```

Start a round with **two or three bots and one human on the keyboard** (a pad as well if you have one; with two keyboards' worth of key sets it is easier to see). Everything should look and feel as before; the things that changed are in 1 to 3 below, the rest is the regression list.

1. **Keyboard takeover.** Press the action-1 key of a keyboard set nobody is using (arrows: RCTRL; keypad: KP+; IJKL: `[`). The selector appears on a bot's portrait (grey keyboard icon); up/down (the set's direction keys) move it between live bots; action-2 cancels; action-1 again takes the bot over. The icon turns the bot's team colour, **the bot stops at once, and the set drives the player from that tick** (walking, laying, selecting). No bot should keep acting for a moment after you take it over.
2. **Pad takeover.** Same with a pad: START opens (the pad buzzes once), d-pad up/down chooses, B/Y cancels, START takes over. The pad then drives the player. If two pads (or a pad and a keyboard) open selectors at the same time, they must never end on the same bot.
3. **Pad unplug (the important one).** With a pad-controlled player in a round (one taken over, or one chosen in the menu), unplug the pad. The player must **revert to a bot at once and carry on playing sensibly from a clean slate** (NOW: it re-plans; BEFORE: it resumed whatever it had been doing when you took it over). Hold an action or a direction as you pull the cable: nothing should be placed afterwards from the held key. Plug the pad back in and take a bot over again: the new player starts with nothing held. A **keyboard** player must never turn into a bot when a pad is unplugged.
4. **Taking a bot, giving it back, taking it again** (unplug and replug, then START twice): each time the player is yours from that moment and a bot from the moment you let go; no stuck movement or action.
5. **Rumble** (needs a pad and a game with a haptic motor): get hit by an explosion or a monster as the pad player; the pad should buzz as before (half strength, a fifth of a second). A keyboard player, and bots, never buzz a pad. Take a bot over with a pad and get hit: it buzzes that pad.
6. **HUD.** Hold action 1 or 2 and press directions: the icon beside your portrait shows the item (block/bomb/mine/rocket; eggs), clears on release; check it for a player taken over mid-round, and that unplugging in the middle of a hold clears it.
7. Regression list from 03a to 04a (rounds, countdown, scoreboard, ESC, play again, map-size change, movement, placing, the tap, two players at once, a slow frame).

Record the result here when it comes back.

**Play-test result:** *(pending)*
