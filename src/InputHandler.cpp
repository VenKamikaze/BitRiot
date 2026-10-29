#include "InputHandler.h"
#include "EntityEnum.h"
#include "GameSettings.h"
#include "MickBaseInput.h"
#include "MickSDLUtil.h"
#include "PlayerCharacterEntity.h"
#include "Simulation.h"
#include <memory>
#include <set>
#include <utility>


namespace
{
// The game's rumble: SDL haptics. (A hit rumbles at half strength for 200 ms.)
class SdlControllerRumble : public ControllerRumble
{
  public:
    void rumble(int controllerId, float strength, unsigned durationMs) override
    {
      MickSDLUtil::rumbleController(controllerId, strength, durationMs);
    }
};
SdlControllerRumble s_sdlRumble;

const float HIT_RUMBLE_STRENGTH = 0.5f;
const unsigned HIT_RUMBLE_MS = 200;
} // namespace

InputHandler::InputHandler()
{
  m_slotController.fill(-1);
  m_rumble = &s_sdlRumble;
  setAvailableKeys();
}

InputHandler::~InputHandler()
{

}

void InputHandler::setPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters,
                               JoinIndicatorView * panel)
{
  //m_numPlayers = playerCharacters.size();
  p_players = playerCharacters;
  p_infoPanel = panel;

  m_keyMap.clear();
  m_sampler.clear(); // a new round starts with every button up and no edge pending
  m_pendingControl.clear();
  m_slotController.fill(-1);
  m_localSlots = 0;
  // Phase 2.7: each game starts with no in-progress join selectors. The
  // InfoPanel is recreated per game so its indicators reset alongside.
  m_pendingJoins.clear();
  // The InputHandler is a singleton reused across games, so the fixed key-set
  // table must be rebuilt each game (see Phase 1). Phase 2.4: m_availableKeySets
  // is now an indexable [0..N) lookup of the four fixed sets, no longer a
  // consume-on-assign pool.
  m_availableKeySets.clear();
  setAvailableKeys();

  std::GameSettings * settings = std::GameSettings::getInstance();

  // Seed the set of keyboard sets already explicitly claimed in the canonical
  // model, so default seeding below can't hand a set to one player that
  // another player already explicitly owns (e.g. P2 explicitly holds set 0).
  std::set<int> usedSets;
  for(auto& p : *settings->getPlayers())
  {
    if(p.attachedKeyboardId >= 0)
    {
      usedSets.insert(p.attachedKeyboardId);
    }
  }

  for(const auto& player: playerCharacters)
  {
    // m_keyMap and the sampler are keyed by SLOT, which is getPlayerNumber() - 1
    // (getID() is an entity id, from EntityFactory's counter, and means nothing
    // here). The canonical model in GameSettings is indexed by the same slot.
    const int slot = player->getPlayerNumber() - 1;
    const int canonicalIndex = slot;
    if (slot < 0 || slot >= std::GameSettings::MAX_PLAYERS)
    {
      continue;
    }

    // Which pad drives the slot is the menu's choice (the canonical model); from here
    // on it is this class's own mapping (brief 04b), changed by takeovers and unplugs.
    Player * setup = settings->getPlayer(canonicalIndex);
    m_slotController[slot] = setup->attachedControllerId;
    if (!player->isControlledByAI())
    {
      m_localSlots |= (1u << slot); // every human is local today
    }

    // Keyboard sets go only to human players without a controller: AI players
    // are never driven by the keyboard, and controller players use the pad.
    if(player->isControlledByAI() || m_slotController[slot] != -1)
    {
      continue;
    }

    // Phase 2.4: assign the keyboard set by the stored attachedKeyboardId
    // (0-based index into the four fixed sets) instead of popping a pool.
    int keyboardSetIndex = setup->attachedKeyboardId;

    // Mandatory default seeding: a human keyboard player with no stored set
    // gets the lowest set neither explicitly claimed by another player nor
    // already handed out earlier in this loop, written back into the
    // canonical model so it stays authoritative (and idempotent across "Play
    // again"). Identical to the old canonicalIndex-based seeding when nothing
    // is explicitly assigned; prevents double-WASD when e.g. P2 explicitly
    // owns set 0. Without this, keyboard-only play (the common case) has no
    // key map.
    if(keyboardSetIndex < 0)
    {
      for(int candidate = 0; candidate < (int) m_availableKeySets.size(); ++candidate)
      {
        if(usedSets.find(candidate) == usedSets.end())
        {
          keyboardSetIndex = candidate;
          break;
        }
      }
      if(keyboardSetIndex >= 0)
      {
        usedSets.insert(keyboardSetIndex);
        settings->setPlayerKeyboard(canonicalIndex, keyboardSetIndex);
      }
    }

    if(keyboardSetIndex >= 0 && keyboardSetIndex < (int) m_availableKeySets.size())
    {
      m_keyMap[slot] = m_availableKeySets.at(keyboardSetIndex);
      // This slot reached here only if it is a human with no controller, i.e. a
      // keyboard player. Show a persistent (player-coloured) keyboard badge so
      // the InfoPanel indicates who is on keyboard - same JOIN_READY state a
      // mid-game keyboard takeover ends in.
      setJoinIndicator(player->getPlayerNumber(), InfoPanel::JOIN_READY, InfoPanel::JOIN_DEVICE_KEYBOARD);
    }
    else
    {
      MickLogger::getInstance()->debug(this, string("setPointers: slot ").append(to_string(slot))
        .append(" has out-of-range keyboard set index ").append(to_string(keyboardSetIndex)).append(" - no keys assigned"));
    }
  }
}

void InputHandler::setAvailableKeys()
{
  array<KEY, NUM_ACTION_BUTTONS> keySet;
  keySet[UP_KEY] = KEY_w;
  keySet[DOWN_KEY] = KEY_s;
  keySet[LEFT_KEY] = KEY_a;
  keySet[RIGHT_KEY] = KEY_d;
  keySet[ACTION1_KEY] = KEY_LCTRL;
  keySet[ACTION2_KEY] = KEY_LSHIFT;
  m_availableKeySets.push_back(keySet);

  keySet[UP_KEY] = KEY_UP;
  keySet[DOWN_KEY] = KEY_DOWN;
  keySet[LEFT_KEY] = KEY_LEFT;
  keySet[RIGHT_KEY] = KEY_RIGHT;
  keySet[ACTION1_KEY] = KEY_RCTRL;
  keySet[ACTION2_KEY] = KEY_RSHIFT;
  m_availableKeySets.push_back(keySet);

  keySet[UP_KEY] = KEY_KP8;
  keySet[DOWN_KEY] = KEY_KP5;
  keySet[LEFT_KEY] = KEY_KP4;
  keySet[RIGHT_KEY] = KEY_KP6;
  keySet[ACTION1_KEY] = KEY_KP_PLUS;
  keySet[ACTION2_KEY] = KEY_KP_MINUS;
  m_availableKeySets.push_back(keySet);

  keySet[UP_KEY] = KEY_i;
  keySet[DOWN_KEY] = KEY_k;
  keySet[LEFT_KEY] = KEY_j;
  keySet[RIGHT_KEY] = KEY_l;
  keySet[ACTION1_KEY] = KEY_LEFTBRACKET;
  keySet[ACTION2_KEY] = KEY_RIGHTBRACKET;
  m_availableKeySets.push_back(keySet);

  // TODO m_keyMap for players 5 and 6
}

shared_ptr<PlayerCharacterEntity> InputHandler::getPlayerAttachedToController(int controllerId)
{
  if (controllerId < 0)
  {
    return nullptr;
  }
  for (int slot = 0; slot < (int) p_players.size() && slot < std::GameSettings::MAX_PLAYERS; slot++)
  {
    if (p_players.at(slot) && m_slotController[slot] == controllerId)
    {
      return p_players.at(slot);
    }
  }
  return nullptr;
}

int InputHandler::controllerForSlot(int slot) const
{
  return (slot >= 0 && slot < std::GameSettings::MAX_PLAYERS) ? m_slotController[slot] : -1;
}

bool InputHandler::isLocalSlot(int slot) const
{
  return slot >= 0 && slot < std::GameSettings::MAX_PLAYERS && (m_localSlots & (1u << slot)) != 0;
}

void InputHandler::setJoinIndicator(int team, JoinIndicatorView::JoinState state, JoinIndicatorView::JoinDevice device)
{
  if (p_infoPanel)
  {
    p_infoPanel->setJoinIndicator(team, state, device);
  }
}

void InputHandler::setRumble(ControllerRumble* rumble)
{
  m_rumble = rumble ? rumble : &s_sdlRumble;
}

void InputHandler::rumbleForHits(const Simulation& sim)
{
  for (int slot = 0; slot < (int) p_players.size() && slot < std::GameSettings::MAX_PLAYERS; slot++)
  {
    const shared_ptr<PlayerCharacterEntity>& player = p_players.at(slot);
    if (player && !player->isControlledByAI() && m_slotController[slot] != -1 && sim.hitThisTick(slot))
    {
      m_rumble->rumble(m_slotController[slot], HIT_RUMBLE_STRENGTH, HIT_RUMBLE_MS);
    }
  }
}

shared_ptr<PlayerCharacterEntity> InputHandler::findPlayerByTeam(int team)
{
  // Player vector is 0-based; entity team is the 1-based slot (see setPointers).
  int idx = team - 1;
  if (idx >= 0 && idx < (int) p_players.size())
  {
    return p_players.at(idx);
  }
  return nullptr;
}

// Take over the chosen 1-based slot with a controller (Phase 2.7). This layer's
// own mapping only - NOT the canonical model (GameSettings), which is the menu seed
// the next round re-seeds from, so a mid-game hot-join must not persist into "Play
// again". Whether the slot is a human or a bot is the simulation's, and changes only
// when it applies the HUMAN_TAKES_OVER event this queues (brief 04b).
void InputHandler::takeOverSlotWithController(int team, int controllerId)
{
  shared_ptr<PlayerCharacterEntity> player = findPlayerByTeam(team);
  if (player)
  {
    const int slot = player->getPlayerNumber() - 1;
    m_slotController[slot] = controllerId;
    m_localSlots |= (1u << slot);
    m_sampler.clearSlot(slot); // nothing the slot's last device left behind
    m_pendingControl.push_back(ControlEvent(slot, ControlEvent::HUMAN_TAKES_OVER));
    MickLogger::getInstance()->debug(this, string("Controller ").append(to_string(controllerId))
      .append(" took over player slot: ").append(to_string(team)));
  }
}

// Take over the chosen 1-based slot with a keyboard set (Phase 2.7). Giving the
// (ex-AI) entity a keyMap entry is what makes it respond to that keyboard set;
// the sampler then reads it like any other human keyboard player. Local mapping
// only, and a HUMAN_TAKES_OVER event for the simulation - see above.
void InputHandler::takeOverSlotWithKeyboard(int team, int keyboardSetIndex)
{
  shared_ptr<PlayerCharacterEntity> player = findPlayerByTeam(team);
  if (player && keyboardSetIndex >= 0 && keyboardSetIndex < (int) m_availableKeySets.size())
  {
    const int slot = player->getPlayerNumber() - 1;
    m_slotController[slot] = -1;
    m_keyMap[slot] = m_availableKeySets.at(keyboardSetIndex);
    m_localSlots |= (1u << slot);
    m_sampler.clearSlot(slot);
    m_pendingControl.push_back(ControlEvent(slot, ControlEvent::HUMAN_TAKES_OVER));
    MickLogger::getInstance()->debug(this, string("Keyboard set ").append(to_string(keyboardSetIndex))
      .append(" took over player slot: ").append(to_string(team)));
  }
}

int InputHandler::findPendingJoinIndex(bool isController, int deviceId)
{
  for (int i = 0; i < (int) m_pendingJoins.size(); i++)
  {
    if (m_pendingJoins.at(i).isController == isController && m_pendingJoins.at(i).deviceId == deviceId)
    {
      return i;
    }
  }
  return -1;
}

// 1-based slots of players that can still be taken over: alive, AI-controlled,
// no controller attached, not already claimed by a takeover that the simulation has
// not applied yet, and not already highlighted by a different joiner.
// The joiner at excludePendingIndex keeps its own current target in the list.
vector<int> InputHandler::getTakeableSlots(int excludePendingIndex)
{
  vector<int> slots;
  for (const auto& player : p_players)
  {
    if (!player || !player->isAlive() || !player->isControlledByAI())
    {
      continue;
    }
    int team = player->getPlayerNumber();
    if (isLocalSlot(team - 1) || controllerForSlot(team - 1) != -1)
    {
      continue;
    }

    bool targetedByOther = false;
    for (int i = 0; i < (int) m_pendingJoins.size(); i++)
    {
      if (i != excludePendingIndex && m_pendingJoins.at(i).targetSlot == team)
      {
        targetedByOther = true;
        break;
      }
    }
    if (!targetedByOther)
    {
      slots.push_back(team);
    }
  }
  return slots;
}

bool InputHandler::openJoin(bool isController, int deviceId)
{
  vector<int> slots = getTakeableSlots(-1);
  if (slots.empty())
  {
    // Nothing to take over (no alive AI). Still consume the trigger so it does
    // not leak into gameplay; the selector simply does not appear.
    return true;
  }

  PendingJoin pj;
  pj.isController = isController;
  pj.deviceId = deviceId;
  pj.targetSlot = slots.front();
  m_pendingJoins.push_back(pj);

  setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NOTREADY,
    pj.isController ? InfoPanel::JOIN_DEVICE_CONTROLLER : InfoPanel::JOIN_DEVICE_KEYBOARD);
  MickLogger::getInstance()->debug(this, string("Join selector opened on slot ").append(to_string(pj.targetSlot)));
  return true;
}

void InputHandler::navigateJoin(int pendingIndex, int direction)
{
  if (pendingIndex < 0 || pendingIndex >= (int) m_pendingJoins.size())
  {
    return;
  }
  PendingJoin& pj = m_pendingJoins.at(pendingIndex);

  vector<int> slots = getTakeableSlots(pendingIndex);
  if (slots.empty())
  {
    return;
  }

  // find the current target's position (default to the first if it has gone)
  int current = 0;
  for (int i = 0; i < (int) slots.size(); i++)
  {
    if (slots.at(i) == pj.targetSlot)
    {
      current = i;
      break;
    }
  }
  int next = ((current + direction) % (int) slots.size() + (int) slots.size()) % (int) slots.size();
  int newSlot = slots.at(next);
  if (newSlot != pj.targetSlot)
  {
    setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NONE);
    pj.targetSlot = newSlot;
    setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NOTREADY,
      pj.isController ? InfoPanel::JOIN_DEVICE_CONTROLLER : InfoPanel::JOIN_DEVICE_KEYBOARD);
  }
}

void InputHandler::confirmJoin(int pendingIndex)
{
  if (pendingIndex < 0 || pendingIndex >= (int) m_pendingJoins.size())
  {
    return;
  }
  PendingJoin pj = m_pendingJoins.at(pendingIndex); // copy before erase

  // Re-validate: the highlighted slot may have died or been taken since the
  // selector opened. getTakeableSlots(pendingIndex) keeps pj's own target if
  // it is still eligible.
  vector<int> slots = getTakeableSlots(pendingIndex);
  bool stillTakeable = false;
  for (int s : slots)
  {
    if (s == pj.targetSlot) { stillTakeable = true; break; }
  }

  if (stillTakeable)
  {
    if (pj.isController)
    {
      takeOverSlotWithController(pj.targetSlot, pj.deviceId);
    }
    else
    {
      takeOverSlotWithKeyboard(pj.targetSlot, pj.deviceId);
    }
    setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_READY,
      pj.isController ? InfoPanel::JOIN_DEVICE_CONTROLLER : InfoPanel::JOIN_DEVICE_KEYBOARD);
  }
  else
  {
    // target no longer valid - drop the indicator, no takeover
    setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NONE);
  }

  m_pendingJoins.erase(m_pendingJoins.begin() + pendingIndex);
}

void InputHandler::cancelJoin(int pendingIndex)
{
  if (pendingIndex < 0 || pendingIndex >= (int) m_pendingJoins.size())
  {
    return;
  }
  setJoinIndicator(m_pendingJoins.at(pendingIndex).targetSlot, InfoPanel::JOIN_NONE);
  m_pendingJoins.erase(m_pendingJoins.begin() + pendingIndex);
}

bool InputHandler::processJoinKeyboardEvent(KEY k)
{
  ButtonName button;
  int setIndex = getKeyboardSetForKey(k, button);
  if (setIndex < 0)
  {
    return false; // not one of our keyboard-set keys
  }

  int pendingIndex = findPendingJoinIndex(false, setIndex);

  if (button == ACTION1_KEY)
  {
    if (pendingIndex >= 0)
    {
      confirmJoin(pendingIndex);
      return true;
    }
    if (!isKeyboardSetBound(setIndex))
    {
      return openJoin(false, setIndex);
    }
    return false; // set drives a live player -> let gameplay handle it
  }

  if (button == ACTION2_KEY)
  {
    if (pendingIndex >= 0)
    {
      cancelJoin(pendingIndex);
      return true;
    }
    return false;
  }

  if (button == UP_KEY || button == DOWN_KEY)
  {
    if (pendingIndex >= 0)
    {
      navigateJoin(pendingIndex, (button == UP_KEY) ? -1 : +1);
      return true;
    }
    return false;
  }

  return false; // left/right do nothing in the selector
}

bool InputHandler::processJoinControllerEvent(int controllerId, Uint8 button)
{
  int pendingIndex = findPendingJoinIndex(true, controllerId);

  if (button == SDL_CONTROLLER_BUTTON_START)
  {
    if (pendingIndex >= 0)
    {
      confirmJoin(pendingIndex);
    }
    else
    {
      openJoin(true, controllerId);
    }
    return true;
  }

  // Action button cancels (mirrors keyboard action2).
  if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_Y)
  {
    if (pendingIndex >= 0)
    {
      cancelJoin(pendingIndex);
    }
    return true;
  }

  if (button == SDL_CONTROLLER_BUTTON_DPAD_UP || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
  {
    if (pendingIndex >= 0)
    {
      navigateJoin(pendingIndex, (button == SDL_CONTROLLER_BUTTON_DPAD_UP) ? -1 : +1);
    }
    return true;
  }

  // Any other button from an unbound controller is eaten (it has no player yet).
  return true;
}

void InputHandler::detachController(int controllerId)
{
  if (controllerId < 0)
  {
    return;
  }
  for (int slot = 0; slot < (int) p_players.size() && slot < std::GameSettings::MAX_PLAYERS; slot++)
  {
    const shared_ptr<PlayerCharacterEntity>& player = p_players.at(slot);
    if (player && player->isAlive() && m_slotController[slot] == controllerId)
    {
      MickLogger::getInstance()->debug(this, string("Detached controller from player: ").append(to_string(player->getPlayerNumber())));
      m_slotController[slot] = -1;
      // Phase 2.6: on unplug, only a player WITHOUT a keymap should revert to CPU.
      // A controller take-over (now via the Phase 2.7 selector, AI-only) leaves
      // the entity with no m_keyMap entry, so on unplug it would be a human with
      // no pad and no keys -> frozen. Reverting it to AI lets
      // PlayerCharacterEntity::update() resume the bot.
      // The m_keyMap-presence guard is kept as defensive code: 2.7 takes over AI
      // slots only (getTakeableSlots filters isControlledByAI), so a controller can
      // no longer hijack a human keyboard player - but should that ever change, a
      // keyboard player (which HAS a keymap) must keep its keys, not become a bot.
      // We deliberately do NOT write the canonical model (GameSettings): it is the
      // menu seed the next round re-seeds from, so a mid-game hot-join must not
      // persist into the next "Play again".
      //
      // Brief 04b: the slot stops being local here and now, and the simulation is told
      // with a RETURN_TO_AI event, which it applies at the start of the next tick.
      if (m_keyMap.find(slot) == m_keyMap.end())
      {
        m_localSlots &= ~(1u << slot);
        m_pendingControl.push_back(ControlEvent(slot, ControlEvent::RETURN_TO_AI));
      }
      m_sampler.clearSlot(slot); // a pad that is gone holds no button down
    }
  }
}


void InputHandler::setControllerButtonState(int slot, ButtonName button, bool down)
{
  m_sampler.setButton(slot, static_cast<PlayerInput::Button>(button), down);
}

void InputHandler::keyboardEvent(KEY k, bool down)
{
  // A key of a keyboard set that drives a human keyboard player. The old per-tick
  // pass skipped AI players and players with a pad attached (the pad drives those),
  // and so does this - by this layer's own view, not the simulation's, so a takeover
  // is routed from the moment it is produced.
  for (const auto& entry : m_keyMap)
  {
    const int slot = entry.first;
    if (!isLocalSlot(slot) || m_slotController[slot] != -1)
    {
      continue;
    }
    for (int b = UP_KEY; b <= ACTION2_KEY; b++)
    {
      if (entry.second[b] == k)
      {
        m_sampler.setButton(slot, static_cast<PlayerInput::Button>(b), down);
      }
    }
  }
}

bool InputHandler::pumpEvents()
{
  shared_ptr<GameRunningSDLInput> input = GameRunningSDLInput::getInstance();
  input->updateEventQueue();
  return input->takeEventCount() > 0;
}

bool InputHandler::quitRequested() const
{
  return GameRunningSDLInput::getInstance()->doQuit();
}

TickInputs InputHandler::sampleTick()
{
  // The local slot mask is this layer's own: every slot a device here drives, the
  // pending takeovers and unplugs included. (A pad or keyboard bound to nobody
  // produces no events at all, so a slot that is not driven samples as no buttons.)
  TickInputs inputs = m_sampler.sampleTick(m_localSlots & InputSampler::ALL_SLOTS);
  inputs.control.swap(m_pendingControl);
  m_pendingControl.clear();
  return inputs;
}


int InputHandler::getPlayerIdForKey(KEY k)
{
  for(auto iter = m_keyMap.begin(); iter != m_keyMap.end(); ++iter)
  {
    for(int buttonNum = ButtonName::UP_KEY; buttonNum != ButtonName::ACTION2_KEY; buttonNum++)
    {
      if(k == m_keyMap[iter->first][buttonNum])
      {
        return iter->first;
      }
    }
  }
  return -1;
}


int InputHandler::getKeyboardSetForActionKey(KEY k)
{
  // Searches the fixed key-set table (always 4 sets, built in the ctor and
  // rebuilt each game by setPointers), NOT m_keyMap which is only populated at
  // game start. This lets the player-select menu identify which keyboard set an
  // action keypress (e.g. LCTRL/RCTRL/KP_PLUS/[) belongs to, before any game.
  for(int i = 0; i < (int) m_availableKeySets.size(); i++)
  {
    if(m_availableKeySets.at(i)[ACTION1_KEY] == k || m_availableKeySets.at(i)[ACTION2_KEY] == k)
    {
      return i;
    }
  }
  return -1;
}

int InputHandler::getKeyboardSetForKey(KEY k, ButtonName & outButton)
{
  // Like getKeyboardSetForActionKey but matches ANY button in a set and reports
  // which button it was - used by the Phase 2.7 join selector to map a raw
  // keydown to (set, button) without touching the per-game m_keyMap.
  for (int i = 0; i < (int) m_availableKeySets.size(); i++)
  {
    for (int b = UP_KEY; b <= ACTION2_KEY; b++)
    {
      if (m_availableKeySets.at(i)[b] == k)
      {
        outButton = (ButtonName) b;
        return i;
      }
    }
  }
  return -1;
}

bool InputHandler::isKeyboardSetBound(int setIndex)
{
  // A set is "bound" if some live player is driven by it. The ACTION1 key is
  // unique per set, so it identifies the set within m_keyMap.
  if (setIndex < 0 || setIndex >= (int) m_availableKeySets.size())
  {
    return false;
  }
  KEY action1 = m_availableKeySets.at(setIndex)[ACTION1_KEY];
  for (const auto& entry : m_keyMap)
  {
    if (entry.second[ACTION1_KEY] == action1)
    {
      return true;
    }
  }
  return false;
}

int InputHandler::getPlayerIdForAction1Button(KEY k)
{
  for(auto iter = m_keyMap.begin(); iter != m_keyMap.end(); ++iter)
  {
    if(k == m_keyMap[iter->first][ACTION1_KEY])
    {
      return iter->first;
    }
  }
  return -1;
}

int InputHandler::getPlayerIdForAction2Button(KEY k)
{
  for(auto iter = m_keyMap.begin(); iter != m_keyMap.end(); ++iter)
  {
    if(k == m_keyMap[iter->first][ACTION2_KEY])
    {
      return iter->first;
    }
  }
  return -1;
}