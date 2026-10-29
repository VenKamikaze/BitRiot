# 04a — Input sampling and `step(inputs)`

- **Repo:** BitRiot
- **Branch:** `netplay/04a-input-sampling`
- **Depends on:** 03b
- **Status:** todo

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

*(fill in when done)*
