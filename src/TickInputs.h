/*
 * TickInputs.h
 *
 * What a human player did during one tick, as the simulation sees it: a byte per
 * slot. The simulation reads nothing else about a human (no keys, no pads, no
 * SDL): a tick's input is a few bytes that can be logged, replayed or sent, and
 * Simulation::step(const TickInputs&) is the only way it gets in.
 *
 * Two halves, kept apart on purpose (brief 04a):
 *
 *   SAMPLE (local, nondeterministic): InputSampler / InputHandler read keyboards and
 *     pads into a PlayerInput per local slot, once per stepped tick. Not sim code.
 *   APPLY (deterministic): Simulation::step() turns every slot's PlayerInput into
 *     movement, item selection and placement, using the per-slot state it keeps
 *     (Simulation::inputState(), which is part of the state hash).
 *
 * SLOT INDEX IS playerNumber - 1: 0-based, slot 0 is player 1 (the entity's
 * playerNumber is 1-based and 0 means the CPU; GameSettings' player slots are
 * 0-based). Getting this backwards is the easiest mistake in this code base.
 *
 * ---------------------------------------------------------------------------
 * THE BITS OF A PlayerInput
 *
 *   bit 0  UP        bit 4  ACTION1            bit 6  ACTION1_RELEASED
 *   bit 1  DOWN      bit 5  ACTION2            bit 7  ACTION2_RELEASED
 *   bit 2  LEFT
 *   bit 3  RIGHT
 *
 * The first six are HELD: the state of that button at the moment of the sample.
 * The two RELEASED bits are EDGES: set when that action button went from down to up
 * at least once since the previous sample. So between two samples:
 *
 *   held at the last sample, still held now ........ HELD
 *   pressed, then held ............................. HELD
 *   held, then released ............................ RELEASED (HELD clear)
 *   pressed and released inside the window (a tap) . RELEASED (HELD clear)
 *   released and pressed again inside the window ... HELD and RELEASED
 *
 * A tap is therefore "RELEASED with the action not down in the simulation", which is
 * how step() tells it from the release of an ongoing hold: the simulation's own
 * per-slot state says whether it saw the press. Direction releases are not
 * recorded: a direction tapped and released inside one window is not seen (as
 * before), and nothing needs it.
 *
 * Releases are per tick, never sticky: a release belongs to the sample that
 * reports it and is forgotten after it. (Before brief 04a a release the game was
 * not looking for stayed pending, and ended the next hold of that action on its
 * first tick.)
 *
 * ---------------------------------------------------------------------------
 * THE APPLY RULES (what Simulation::step() does with a slot's PlayerInput)
 *
 * Each tick, for each slot whose player exists, is alive and is not controlled by
 * the AI (every other slot's bits are ignored). "A1" and "A2" are the action
 * buttons; a "hold" is the simulation's per-slot state (action1Down or
 * action2Down, plus the item the hold has selected). "Place" means: if the
 * player's tile has no static entity, the player lays that item (and pays for it);
 * either way the hold ends and the selection goes back to BLOCK.
 *
 *  state       input                                   result
 *  ----------  --------------------------------------  ------------------------------
 *  idle        UP / DOWN / LEFT / RIGHT held           The player moves one step that
 *  (no hold)   (precedence up > down > left >          way and its animation advances.
 *              right: only the first one counts)       This happens first, and the
 *                                                      action rows below follow on
 *                                                      the same tick.
 *
 *              A1 held, or A1_RELEASED                 An A1 hold begins (selection
 *              (A1 wins over A2 if both)               BLOCK). Movement is blocked
 *                                                      from the next tick on.
 *              ... and A1_RELEASED is set              THE TAP RULE (the one change
 *              (press and release in one tick)         to the game): the hold begins
 *                                                      and ends on this tick. Places
 *                                                      the default, BLOCK.
 *
 *              A2 held, or A2_RELEASED, and A1         An A2 hold begins (selection
 *              is neither held nor released            EGG1).
 *              ... and A2_RELEASED is set              The tap rule again: places the
 *                                                      default, EGG1.
 *
 *              A2_RELEASED while A1 is held or         Dropped: A1 has priority, as
 *              A1_RELEASED is set                      a pressed A2 always lost to
 *                                                      A1. The A2 tap is not kept.
 *
 *  A1 hold     No movement. The held direction selects (precedence up > down >
 *  (BLOCK      left > right) and the selection sticks when the direction is let go:
 *  until       UP -> BOMB, DOWN -> BLOCK, LEFT -> MINE, RIGHT -> ROCKET_SPIN.
 *  changed)    A1_RELEASED (acted on whether or not A1 is held again, after this
 *              tick's direction was applied): Place the selection.
 *              A2 held, pressed or released while the hold lasts: ignored.
 *              (A2, if still held, starts a hold on the tick after this one ends.)
 *
 *  A2 hold     No movement. The held direction selects the egg (the hold begins
 *  (EGG1       with EGG1): UP -> EGG3, DOWN -> EGG5, LEFT -> EGG2, RIGHT -> EGG4.
 *  until       A2_RELEASED: Place the selected egg.
 *  changed)    A1 held, pressed or released while the hold lasts: ignored.
 *
 * "Ignored" is what the game always did: a release counts only inside its own
 * action's hold, and the other action's press during a hold is neither acted on
 * nor remembered. Two changes from the game before brief 04a:
 *   1. the tap rule above (a press and release inside one tick used to be lost);
 *   2. a release the rules did not use used to stay pending and end the next hold
 *      of that action on its first tick ("the stale release"); it now goes with
 *      its tick, so the rules above are the whole story.
 *
 * Defaults, checked against PlayerCharacterEntity::createEntity and the old
 * InputHandler::processPlayerButtons: an A1 hold with no direction selected places
 * a BLOCK (the selection is BLOCK whenever no hold is on), and a pressed A2
 * selects EGG1 at once.
 * ---------------------------------------------------------------------------
 */

#ifndef _TICKINPUTS_H
#define _TICKINPUTS_H

#include <array>
#include <cstdint>
#include <vector>

#include "GameSettings.h"

// One slot's input for one tick. One byte; the bits are described at the top of
// this file.
struct PlayerInput
{
  // The six physical buttons. The values are the bit numbers of their HELD bits.
  enum Button { UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3, ACTION1 = 4, ACTION2 = 5 };
  static constexpr int NUM_BUTTONS = 6;

  static constexpr uint8_t HELD_MASK = 0x3F;
  static constexpr uint8_t ACTION1_RELEASED = 1 << 6;
  static constexpr uint8_t ACTION2_RELEASED = 1 << 7;

  uint8_t bits = 0;

  PlayerInput() = default;
  explicit PlayerInput(uint8_t b) : bits(b) {}

  static uint8_t heldBit(Button b) { return (uint8_t) (1u << b); }
  // Only the two actions have a released bit; for the others this is 0.
  static uint8_t releasedBit(Button b) { return b == ACTION1 ? ACTION1_RELEASED : (b == ACTION2 ? ACTION2_RELEASED : 0); }

  bool held(Button b) const { return (bits & heldBit(b)) != 0; }
  bool released(Button b) const { return (bits & releasedBit(b)) != 0; }

  PlayerInput& setHeld(Button b, bool on = true)
  {
    bits = on ? (uint8_t) (bits | heldBit(b)) : (uint8_t) (bits & ~heldBit(b));
    return *this;
  }
  PlayerInput& setReleased(Button b, bool on = true)
  {
    const uint8_t bit = releasedBit(b);
    bits = on ? (uint8_t) (bits | bit) : (uint8_t) (bits & ~bit);
    return *this;
  }

  bool operator==(const PlayerInput& o) const { return bits == o.bits; }
  bool operator!=(const PlayerInput& o) const { return bits != o.bits; }
};

// A change to who controls a slot: a join, a takeover, an unplug. Brief 04b defines
// these. Until then the type exists so that TickInputs has its slot, and
// Simulation::step() refuses a TickInputs that carries one rather than ignore it.
struct ControlEvent
{
};

// Everything the simulation is told about one tick.
struct TickInputs
{
  // Indexed by SLOT (playerNumber - 1). Slots beyond the round's player count, and
  // slots nobody sampled, are all-zero (no buttons).
  std::array<PlayerInput, std::GameSettings::MAX_PLAYERS> slot;
  // Applied before the inputs. Empty until brief 04b.
  std::vector<ControlEvent> control;
};

#endif /* _TICKINPUTS_H */
