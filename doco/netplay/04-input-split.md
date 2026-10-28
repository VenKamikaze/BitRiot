# 04 — Split input sampling from input application

- **Repo:** BitRiot
- **Branch:** `netplay/04-input-split`
- **Depends on:** 03
- **Status:** todo

## Goal

Split human input into two stages:

- **Sample** (local, nondeterministic): read the keyboard and controllers into a compact per-slot bitmask for this tick.
- **Apply** (deterministic, inside the sim): `Simulation::step(const TickInputs&)` turns everyone's bitmasks into movement, selection and placement.

All per-player input *state* moves out of `InputHandler` into sim-owned, per-slot state. Changes of control (AI to human and back) become explicit sim events. Once this is in, a tick's full input is a few bytes that can be logged, replayed or sent over a network.

## Read first

- [`research/code-facts.md`](research/code-facts.md) §6 (input) is essential: it covers the semantics, quirks, the state held in `InputHandler`, the join/takeover path, hotplug and rumble. Also read §1 (ESC as an exception).
- `src/InputHandler.{h,cpp}` (`processKeyboardInput`, `processPlayerButtons`, the join selector, `detachController`, `setPointers`).
- `src/GameRunningSDLInput.{h,cpp}` (the event pump, key/controller state, sticky releases).
- `src/PlayerCharacterEntity.h` (the public `m_controlledByAI`).
- Brief 03's `Simulation` API.

## Design

- **The `PlayerInput` bitmask.** One byte per slot per tick:
  - **6 "held" bits:** UP, DOWN, LEFT, RIGHT, ACTION1, ACTION2.
  - **2 "released this tick" bits:** ACTION1_RELEASED, ACTION2_RELEASED.

  Actions are press-to-select, release-to-place. Releases are explicit bits because a press and release can both happen within one tick.

  Define the edge semantics precisely in the header comment. The sampler records a release that happened at any point since the last sample, including press+release within one tick, so a quick tap that sets a release bit still places something.

  This intentionally fixes today's quirk. A tap shorter than one tick is currently lost, but its release stays pending and can trigger a premature placement later. Document the behaviour change.

- **Direction precedence** (up > down > left > right) stays exactly as it is, applied inside the sim from the held bits.
- **`TickInputs`**: an `std::array<PlayerInput, MAX_PLAYERS>` plus a list of `ControlEvent`s for this tick.
- **`ControlEvent`**: `{slot, kind}`, where `kind` is one of:
  - `HUMAN_TAKES_OVER` (AI → human)
  - `RETURN_TO_AI` (human → AI)
  - `LEAVE`, for netplay later; it can alias `RETURN_TO_AI` for now.

  The sim applies events at the start of the tick, before inputs. The join selector, controller unplug and keyboard takeover produce events; they no longer flip `m_controlledByAI` directly. Make `m_controlledByAI` private, with access through the sim.

  Decide what happens to the AI's goal and instruction lists on takeover and on return. Today they persist. Clearing them on both transitions is probably the deterministic, least-surprising choice. State your choice.

- **Per-slot sim state:** `m_action1Down`, `m_action2Down` and `m_spawnSelection` move from `InputHandler` (keyed by entity id) into per-slot state in the sim, either on `PlayerCharacterEntity` or in a sim-owned array.
- **What stays local, outside the sim:**
  - **InfoPanel selection highlight.** The sim exposes the per-slot selection, and the UI reads it.
  - **Rumble.** The sim records "slot N was hit this tick", and the local layer rumbles the device bound to slot N, if any. Rumble must not be driven from inside `onCollision`.
- **ESC/quit** stays a local action, outside the sim. It must no longer be an exception thrown through `Simulation::step`. Make it a flag or return value checked by `GameEngine`. In netplay (brief 07) it becomes `LEAVE`.
- **Where the device-to-slot mapping lives:** in the local input layer. That covers keyboard sets, `attachedKeyboardId`/`attachedControllerId` and hotplug. The sim only ever sees slots.

## In scope

- `InputSampler`, or a refactored `InputHandler` sampling side: SDL state → `TickInputs` for local slots.
- `Simulation::step(const TickInputs&)` applies control events, then inputs, then the existing tick.
- Move the per-slot state, and convert the join, takeover and unplug paths to events.
- Rumble and selection-highlight plumbing, as above.
- `GameEngine` wiring: sample → `step(inputs)` → render. Local play must feel identical apart from the documented tap fix.

## Out of scope

- Recording and replay (05).
- Networking (07).
- New controls or keymaps.

## Tests

The harness can now inject input without SDL.

- Held directions move a human-controlled player. Precedence holds.
- **Press-then-release** of ACTION1 places the selected item. **Press and release in the same tick** also places it: the new semantics.
- **Selection cycling** with the other action (check today's actual behaviour first, and test that).
- **Control events:** `HUMAN_TAKES_OVER` stops AI decisions for that slot, and `RETURN_TO_AI` resumes them, on the exact tick.
- **Determinism:** the same seed with the same scripted `TickInputs` sequence gives an identical `stateHash()` per tick across two runs.
- Prove the new tests fail on the pre-change code where that applies (for example, the same-tick tap).
- Run a 200-seed sweep.

## Acceptance criteria

- No sim code reads SDL or input devices.
- `InputHandler` holds no per-player game state.
- `m_controlledByAI` changes only through `ControlEvent`s.
- Input can be scripted in tests.
- The semantics change is documented in the header and in the Outcome.
- Local play works with keyboard and controller, including join/takeover and unplug. Do a manual check, or ask the user to play-test, and say which in the Outcome.

## Outcome

*(fill in when done)*
