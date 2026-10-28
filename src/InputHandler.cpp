#include "InputHandler.h"
#include "EntityEnum.h"
#include "GameSettings.h"
#include "MickBaseInput.h"
#include "PlayerCharacterEntity.h"
#include <memory>
#include <set>
#include <utility>


InputHandler::InputHandler()
{
  setAvailableKeys();
}

InputHandler::~InputHandler()
{

}

void InputHandler::setPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters,
                               DynamicMap * dynamicMap, InfoPanel * panel)
{
  //m_numPlayers = playerCharacters.size();
  p_players = playerCharacters;
  p_dynamicMap = dynamicMap;
  p_infoPanel = panel;

  m_action1Down.clear();
  m_action2Down.clear();
  m_spawnSelection.clear();
  m_keyMap.clear();
  m_controllerDown.clear();
  m_controllerReleased.clear();
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
    // getID() is an entity id from EntityFactory's counter (restarted each round by
    // EntityManager's constructor), NOT a player slot. It is only safe here as an opaque key for the
    // m_keyMap / m_action* maps (consistent with processKeyboardInput). The
    // canonical model is indexed by player slot, which is getPlayerNumber() (1-based).
    const int id = player->getID();                   // opaque unique entity id (map key)
    const int canonicalIndex = player->getPlayerNumber() - 1; // 0-based player slot -> model index

    m_action1Down.insert(std::pair<int, bool>(id, false));
    m_action2Down.insert(std::pair<int, bool>(id, false));
    m_spawnSelection.insert(std::pair<int, EntityType>(id, BLOCK));

    // Keyboard sets go only to human players without a controller: AI players
    // are never driven by the keyboard, and controller players use the pad.
    if(player->isControlledByAI() || player->attachedController != -1)
    {
      continue;
    }

    // Phase 2.4: assign the keyboard set by the stored attachedKeyboardId
    // (0-based index into the four fixed sets) instead of popping a pool.
    Player * setup = settings->getPlayer(canonicalIndex);
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
      m_keyMap[id] = m_availableKeySets.at(keyboardSetIndex);
      // This slot reached here only if it is a human with no controller, i.e. a
      // keyboard player. Show a persistent (player-coloured) keyboard badge so
      // the InfoPanel indicates who is on keyboard - same JOIN_READY state a
      // mid-game keyboard takeover ends in.
      if (p_infoPanel)
      {
        p_infoPanel->setJoinIndicator(player->getPlayerNumber(), InfoPanel::JOIN_READY,
          InfoPanel::JOIN_DEVICE_KEYBOARD);
      }
    }
    else
    {
      MickLogger::getInstance()->debug(this, string("setPointers: player ").append(to_string(id))
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
  for(const auto& player : p_players)
  {
    if(player && player->attachedController == controllerId)
    {
      return player;
    }
  }
  return nullptr;
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

// Take over the chosen 1-based slot with a controller (Phase 2.7). Live entity
// only - NOT the canonical model (GameSettings), which is the menu seed the next
// round re-seeds from, so a mid-game hot-join must not persist into "Play again".
void InputHandler::takeOverSlotWithController(int team, int controllerId)
{
  shared_ptr<PlayerCharacterEntity> player = findPlayerByTeam(team);
  if (player)
  {
    player->attachedController = controllerId;
    player->m_controlledByAI = false; // FIXME duplication as we already have a playerAI flag in gamesettings...
    MickLogger::getInstance()->debug(this, string("Controller ").append(to_string(controllerId))
      .append(" took over player slot: ").append(to_string(team)));
  }
}

// Take over the chosen 1-based slot with a keyboard set (Phase 2.7). Giving the
// (ex-AI) entity a keyMap entry is what makes it respond to that keyboard set;
// processKeyboardInput then drives it like any other human keyboard player. The
// action1/action2/spawn maps already hold an entry for every entity (seeded for
// all players, AI included, in setPointers). Live entity only - see above.
void InputHandler::takeOverSlotWithKeyboard(int team, int keyboardSetIndex)
{
  shared_ptr<PlayerCharacterEntity> player = findPlayerByTeam(team);
  if (player && keyboardSetIndex >= 0 && keyboardSetIndex < (int) m_availableKeySets.size())
  {
    player->attachedController = -1;
    player->m_controlledByAI = false;
    m_keyMap[player->getID()] = m_availableKeySets.at(keyboardSetIndex);
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
// no controller attached, and not already highlighted by a different joiner.
// The joiner at excludePendingIndex keeps its own current target in the list.
vector<int> InputHandler::getTakeableSlots(int excludePendingIndex)
{
  vector<int> slots;
  for (const auto& player : p_players)
  {
    if (!player || !player->isAlive() || !player->isControlledByAI() || player->attachedController != -1)
    {
      continue;
    }
    int team = player->getPlayerNumber();

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

  p_infoPanel->setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NOTREADY,
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
    p_infoPanel->setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NONE);
    pj.targetSlot = newSlot;
    p_infoPanel->setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NOTREADY,
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
    p_infoPanel->setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_READY,
      pj.isController ? InfoPanel::JOIN_DEVICE_CONTROLLER : InfoPanel::JOIN_DEVICE_KEYBOARD);
  }
  else
  {
    // target no longer valid - drop the indicator, no takeover
    p_infoPanel->setJoinIndicator(pj.targetSlot, InfoPanel::JOIN_NONE);
  }

  m_pendingJoins.erase(m_pendingJoins.begin() + pendingIndex);
}

void InputHandler::cancelJoin(int pendingIndex)
{
  if (pendingIndex < 0 || pendingIndex >= (int) m_pendingJoins.size())
  {
    return;
  }
  p_infoPanel->setJoinIndicator(m_pendingJoins.at(pendingIndex).targetSlot, InfoPanel::JOIN_NONE);
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
  for(const auto& player : p_players)
  {
    if(player && player->isAlive() && player->attachedController == controllerId)
    {
      MickLogger::getInstance()->debug(this, string("Detached controller from player: ").append(to_string(player->getPlayerNumber())));
      player->attachedController = -1;
      // Phase 2.6: on unplug, only a player WITHOUT a keymap should revert to CPU.
      // A controller take-over (now via the Phase 2.7 selector, AI-only) leaves
      // the entity with no m_keyMap entry, so on unplug it would be
      // attachedController==-1 AND non-AI -> not skipped by processKeyboardInput
      // but has no keys -> frozen. Reverting it to AI lets
      // PlayerCharacterEntity::update() resume the bot (self-heals empty
      // goal/instruction lists; keys off the flag at PlayerCharacterEntity.cpp:68).
      // The m_keyMap-presence guard is kept as defensive code: 2.7 takes over AI
      // slots only (getTakeableSlots filters isControlledByAI), so a controller can
      // no longer hijack a human keyboard player - but should that ever change, a
      // keyboard player (which HAS a keymap) must keep its keys, not become a bot.
      // We deliberately do NOT write the canonical model (GameSettings): it is the
      // menu seed the next round re-seeds from, so a mid-game hot-join must not
      // persist into the next "Play again".
      if (m_keyMap.find(player->getID()) == m_keyMap.end())
      {
        player->m_controlledByAI = true;
      }
    }
  }
}


void InputHandler::processPlayerButtons(shared_ptr<PlayerCharacterEntity> player,
    const std::function<bool(ButtonName)>& isDown,
    const std::function<bool(ButtonName)>& isReleased)
{
  int i = player->getID();           // opaque unique entity id: m_action* key
  int slot = player->getPlayerNumber();      // 1-based player slot: InfoPanel row (see setSelection)

  if (m_action1Down[i] == false && m_action2Down[i] == false)
  {
    if (isDown(UP_KEY))
    {
      player->moveInDirection(UP);
      player->incAnimFrame();
    }
    else if (isDown(DOWN_KEY))
    {
      player->moveInDirection(DOWN);
      player->incAnimFrame();
    }
    else if (isDown(LEFT_KEY))
    {
      player->moveInDirection(LEFT);
      player->incAnimFrame();
    }
    else if (isDown(RIGHT_KEY))
    {
      player->moveInDirection(RIGHT);
      player->incAnimFrame();
    }
    if (isDown(ACTION1_KEY))
    {
      m_action1Down[i] = true;
    }
    else if (isDown(ACTION2_KEY))
    {
      m_action2Down[i] = true;
      m_spawnSelection[i] = EGG1;
    }
  }
  else // action1Down == true || action2Down == true
  {
    if (m_action1Down[i] == true)
    {
      // get action1 + direction
      if (isDown(UP_KEY))
      {
        m_spawnSelection[i] = BOMB;
        p_infoPanel->setSelection(BOMB, slot);
      }
      else if (isDown(DOWN_KEY))
      {
        m_spawnSelection[i] = BLOCK;
        p_infoPanel->setSelection(BLOCK, slot);
      }
      else if (isDown(LEFT_KEY))
      {
        m_spawnSelection[i] = MINE;
        p_infoPanel->setSelection(MINE, slot);
      }
      else if (isDown(RIGHT_KEY))
      {
        m_spawnSelection[i] = ROCKET_SPIN;
        p_infoPanel->setSelection(ROCKET_SPIN, slot);
      }

      // for no direction press
      if (m_spawnSelection[i] == BLOCK)
      {
        p_infoPanel->setSelection(BLOCK, slot);
      }

      if (isReleased(ACTION1_KEY))
      {
        // control released this frame, try to place item
        if (p_dynamicMap->tileHasStaticEntity(player->getTileX(), player->getTileY()) == false)
        {
          // TO DO - if trying to place block, check there is an exit
          player->createEntity(m_spawnSelection[i]);
        }

        m_action1Down[i] = false;
        m_spawnSelection[i] = BLOCK;
        p_infoPanel->setSelection(PLAYER_CHARACTER, slot);
      }
      // end if action1Down == true
    }
    else
    {
      // action2Down == true
      // get action2 + direction
      if (isDown(UP_KEY))
      {
        m_spawnSelection[i] = EGG3;
      }
      else if (isDown(DOWN_KEY))
      {
        m_spawnSelection[i] = EGG5;
      }
      else if (isDown(LEFT_KEY))
      {
        m_spawnSelection[i] = EGG2;
      }
      else if (isDown(RIGHT_KEY))
      {
        m_spawnSelection[i] = EGG4;
      }

      p_infoPanel->setSelection(m_spawnSelection[i], slot);


      if (isReleased(ACTION2_KEY))
      {
        // action2 released this frame, try to place egg
        if (p_dynamicMap->tileHasStaticEntity(player->getTileX(), player->getTileY()) == false)
        {
          player->createEntity(m_spawnSelection[i]);
        }

        m_action2Down[i] = false;
        m_spawnSelection[i] = BLOCK;
        p_infoPanel->setSelection(PLAYER_CHARACTER, slot);
      }

    }
  } // end if action1 && action2 down
}

void InputHandler::setControllerButtonState(int playerId, ButtonName button, bool down)
{
  bool wasDown = m_controllerDown[playerId][button]; // lazily zero-inits on first touch
  m_controllerDown[playerId][button] = down;
  if (wasDown && ! down)
  {
    m_controllerReleased[playerId][button] = true;
  }
}

bool InputHandler::consumeControllerReleased(int playerId, ButtonName button)
{
  auto it = m_controllerReleased.find(playerId);
  if (it == m_controllerReleased.end() || ! it->second[button])
  {
    return false;
  }
  it->second[button] = false; // edge-triggered: only true once, mirrors isKeyReleased()
  return true;
}

void InputHandler::processKeyboardInput(/*SDL_Event event*/)
{
  shared_ptr<MickBaseInput> input = GameRunningSDLInput::getInstance();
  input->updateEventQueue();

  if(input->doQuit())
  {
    MickLogger::getInstance()->debug(this, "Escape pressed in game");
    throw InputException("Escape pressed", GOT_QUIT);
  }

  if(input->isAKeyDown() || input->isAKeyReleased())
  {
    for(const auto& player : p_players)
    {
      // Skip AI players and controller players: only human keyboard players are
      // driven here. The attachedController check closes the Phase 2.3
      // intermediate state where a controller player (still non-AI) would
      // otherwise respond to both the pad and a keyboard set.
      if ((!player) || (!player->isAlive()) || (player->isControlledByAI()) || (player->attachedController != -1))
      {
        continue;
      }

      int i = player->getID();
      processPlayerButtons(player,
        [&](ButtonName b){ return input->isKeyDown(m_keyMap[i][b]); },
        [&](ButtonName b){ return input->isKeyReleased(m_keyMap[i][b]); });
    }
  }

  // Controller-driven players: processed independently of the keyboard gate
  // above (a controller-only game may see zero real key activity) and of the
  // shared keysCurrentlyDown/m_keyMap machinery entirely - see the
  // m_controllerDown/m_controllerReleased comment in InputHandler.h for why.
  for(const auto& player : p_players)
  {
    if ((!player) || (!player->isAlive()) || (player->isControlledByAI()) || (player->attachedController == -1))
    {
      continue;
    }

    int i = player->getID();
    processPlayerButtons(player,
      [&](ButtonName b){ return m_controllerDown[i][b]; },
      [&](ButtonName b){ return consumeControllerReleased(i, b); });
  }
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