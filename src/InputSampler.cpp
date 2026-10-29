#include "InputSampler.h"

void InputSampler::setButton(int slot, PlayerInput::Button button, bool down)
{
  if (slot < 0 || slot >= std::GameSettings::MAX_PLAYERS)
  {
    return;
  }
  SlotState& s = m_slots[slot];
  const uint8_t bit = PlayerInput::heldBit(button);
  if (down)
  {
    s.held = (uint8_t) (s.held | bit);
  }
  else if (s.held & bit)
  {
    s.held = (uint8_t) (s.held & ~bit);
    s.released = (uint8_t) (s.released | PlayerInput::releasedBit(button)); // 0 for a direction
  }
}

PlayerInput InputSampler::sample(int slot)
{
  if (slot < 0 || slot >= std::GameSettings::MAX_PLAYERS)
  {
    return PlayerInput();
  }
  SlotState& s = m_slots[slot];
  const PlayerInput in((uint8_t) (s.held | s.released));
  s.released = 0;
  return in;
}

TickInputs InputSampler::sampleTick(uint32_t localSlotMask)
{
  TickInputs out;
  for (int slot = 0; slot < std::GameSettings::MAX_PLAYERS; slot++)
  {
    if (localSlotMask & (1u << slot))
    {
      out.slot[slot] = sample(slot);
    }
    else
    {
      m_slots[slot].released = 0;
    }
  }
  return out;
}

void InputSampler::clearSlot(int slot)
{
  if (slot >= 0 && slot < std::GameSettings::MAX_PLAYERS)
  {
    m_slots[slot] = SlotState();
  }
}

void InputSampler::clear()
{
  m_slots = std::array<SlotState, std::GameSettings::MAX_PLAYERS>();
}

PlayerInput InputSampler::peek(int slot) const
{
  if (slot < 0 || slot >= std::GameSettings::MAX_PLAYERS)
  {
    return PlayerInput();
  }
  return PlayerInput((uint8_t) (m_slots[slot].held | m_slots[slot].released));
}
