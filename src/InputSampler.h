/*
 * InputSampler.h
 *
 * The local, nondeterministic half of input (brief 04a): it turns "this button of
 * this slot went down / up" events, in whatever order and at whatever rate the
 * devices produce them, into one PlayerInput per slot per stepped tick. It knows
 * nothing about SDL, keys or pads: the device layer (InputHandler) maps a key or a
 * controller button to a (slot, button) and calls setButton(). SLOT is
 * playerNumber - 1 (see TickInputs.h).
 *
 * Events and samples are decoupled. The frame loop pumps events every frame, so
 * they accumulate here, and takes a sample only on a tick that is actually stepped:
 * a tick held back by canStep() (netplay) or a frame that runs several ticks loses
 * nothing.
 *
 *   held bits ........ the state of each button at the moment of the sample;
 *   released bits .... did that ACTION button go from down to up since the last
 *                      sample (an edge, consumed by the sample that reports it).
 *
 * See the top of TickInputs.h for what the bits mean to the simulation, including a
 * tap (pressed and released between two samples: RELEASED set, HELD clear). Edges
 * are recorded per tick and never sticky: sample() forgets the releases it reported.
 * If two taps of one button fall into one window they are one RELEASED bit.
 *
 * LOCAL SLOT MASK. sampleTick() takes the set of slots this machine reads input
 * for (bit n is slot n). Today every human slot is local; with netplay only the
 * local players' slots are, and the rest arrive from the network. Slots outside the
 * mask report no buttons, and their pending edges are dropped rather than kept, so
 * a release cannot wait around to fire when the slot later becomes local.
 */

#ifndef _INPUTSAMPLER_H
#define _INPUTSAMPLER_H

#include <array>
#include <cstdint>

#include "TickInputs.h"

class InputSampler
{
  public:
    // Every slot is local: the mask for a machine that reads all the input.
    static const uint32_t ALL_SLOTS = (1u << std::GameSettings::MAX_PLAYERS) - 1;

    // A button of a slot's device changed state. A press of a button that is already
    // down (key repeat) and a release of one that is not down change nothing.
    // Out-of-range slots are ignored.
    void setButton(int slot, PlayerInput::Button button, bool down);

    // One slot's input for the tick being stepped now. Consumes that slot's released
    // edges.
    PlayerInput sample(int slot);

    // The input for every slot in localSlotMask for the tick being stepped now. The
    // other slots are all-zero in the result, and their pending edges are dropped.
    TickInputs sampleTick(uint32_t localSlotMask = ALL_SLOTS);

    // Forget everything about one slot (its device changed hands, or it went to the
    // AI), or about all of them (a new round).
    void clearSlot(int slot);
    void clear();

    // For tests and diagnostics: the buttons currently down, and the release edges
    // waiting for the next sample, without consuming them.
    PlayerInput peek(int slot) const;

  private:
    struct SlotState
    {
      uint8_t held = 0;     // bit n: PlayerInput::Button n is down
      uint8_t released = 0; // PlayerInput::ACTION1_RELEASED / ACTION2_RELEASED bits
    };
    std::array<SlotState, std::GameSettings::MAX_PLAYERS> m_slots;
};

#endif /* _INPUTSAMPLER_H */
