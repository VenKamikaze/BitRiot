# 04b — Control events, rumble and UI plumbing

- **Repo:** BitRiot
- **Branch:** `netplay/04b-control-events`
- **Depends on:** 04a
- **Status:** todo

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

*(fill in when done)*
