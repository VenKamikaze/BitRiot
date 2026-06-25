#include "InputHandler.h"
#include "EntityEnum.h"
#include "GameSettings.h"
#include "MickBaseInput.h"
#include "PlayerCharacterEntity.h"
#include <memory>
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
  // The InputHandler is a singleton reused across games, so the fixed key-set
  // table must be rebuilt each game (see Phase 1). Phase 2.4: m_availableKeySets
  // is now an indexable [0..N) lookup of the four fixed sets, no longer a
  // consume-on-assign pool.
  m_availableKeySets.clear();
  setAvailableKeys();

  std::GameSettings * settings = std::GameSettings::getInstance();

  for(const auto& player: playerCharacters)
  {
    // getID() is a globally-unique entity id from EntityFactory's running
    // counter (the factory is a never-reset singleton), NOT a player slot - by
    // the 2nd game it is ~1980+. It is only safe here as an opaque key for the
    // m_keyMap / m_action* maps (consistent with processKeyboardInput). The
    // canonical model is indexed by player slot, which is getTeam() (1-based).
    const int id = player->getID();                   // opaque unique entity id (map key)
    const int canonicalIndex = player->getTeam() - 1; // 0-based player slot -> model index

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
    // gets a deterministic set by player index, written back into the canonical
    // model so it stays authoritative (and idempotent across "Play again").
    // Without this, keyboard-only play (the common case) has no key map.
    if(keyboardSetIndex < 0)
    {
      keyboardSetIndex = canonicalIndex;
      settings->setPlayerKeyboard(canonicalIndex, keyboardSetIndex);
    }

    if(keyboardSetIndex >= 0 && keyboardSetIndex < (int) m_availableKeySets.size())
    {
      m_keyMap[id] = m_availableKeySets.at(keyboardSetIndex);
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

shared_ptr<PlayerCharacterEntity> InputHandler::attachNewControllerToPlayer(int controllerId)
{
  for (int i = p_players.size()-1; i >= 0; i--)
  {
    shared_ptr<PlayerCharacterEntity> player = p_players.at(i);
    if(player && player->isAlive() && player->attachedController == -1)
    {
      player->attachedController=controllerId;
      player->m_controlledByAI=false; // FIXME duplication as we already have a playerAI flag in gamesettings...
      MickLogger::getInstance()->debug(this, string("Attaching controller to player: ").append(to_string(i)));
      return player;
    }
  }
  return nullptr;
}

void InputHandler::detachController(int controllerId)
{
  for(const auto& player : p_players)
  {
    if(player && player->isAlive() && player->attachedController == controllerId)
    {
      MickLogger::getInstance()->debug(this, string("Detached controller from player: ").append(to_string(player->getTeam())));
      player->attachedController = -1;
    }
  }
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
  else if(input->isAKeyDown() || input->isAKeyReleased())
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

      int i = player->getID();           // opaque unique entity id: m_keyMap / m_action* key
      int slot = player->getTeam();      // 1-based player slot: InfoPanel row (see setSelection)

      //static bool action1Down[GameSettings::MAX_PLAYERS] = { false, false, false, false };
      //static bool action2Down[4] = { false, false, false, false };
      //static EntityType selection[4] = { BLOCK, BLOCK, BLOCK, BLOCK };

      if (m_action1Down[i] == false && m_action2Down[i] == false)
      {
        if (input->isKeyDown(m_keyMap[i][UP_KEY]))
        {
          //cout << "UP: KEY IS DOWN!\n";
          player->moveInDirection(UP);
          player->incAnimFrame();
        }
        else if (input->isKeyDown(m_keyMap[i][DOWN_KEY]))
        {
          player->moveInDirection(DOWN);
          player->incAnimFrame();
        }
        else if (input->isKeyDown(m_keyMap[i][LEFT_KEY]))
        {
          player->moveInDirection(LEFT);
          player->incAnimFrame();
        }
        else if (input->isKeyDown(m_keyMap[i][RIGHT_KEY]))
        {
          player->moveInDirection(RIGHT);
          player->incAnimFrame();
        }
        if (input->isKeyDown(m_keyMap[i][ACTION1_KEY]))
        {
          m_action1Down[i] = true;
        }
        else if (input->isKeyDown(m_keyMap[i][ACTION2_KEY]))
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
          if (input->isKeyDown(m_keyMap[i][UP_KEY]))
          {
            m_spawnSelection[i] = BOMB;
            p_infoPanel->setSelection(BOMB, slot);
          }
          else if (input->isKeyDown(m_keyMap[i][DOWN_KEY]))
          {
            m_spawnSelection[i] = BLOCK;
            p_infoPanel->setSelection(BLOCK, slot);
          }
          else if (input->isKeyDown(m_keyMap[i][LEFT_KEY]))
          {
            m_spawnSelection[i] = MINE;
            p_infoPanel->setSelection(MINE, slot);
          }
          else if (input->isKeyDown(m_keyMap[i][RIGHT_KEY]))
          {
            m_spawnSelection[i] = ROCKET_SPIN;
            p_infoPanel->setSelection(ROCKET_SPIN, slot);
          }

          // for no direction press
          if (m_spawnSelection[i] == BLOCK)
          {
            p_infoPanel->setSelection(BLOCK, slot);
          }

          if (input->isKeyReleased(m_keyMap[i][ACTION1_KEY]))
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
          if (input->isKeyDown(m_keyMap[i][UP_KEY]))
          {
            m_spawnSelection[i] = EGG3;
          }
          else if (input->isKeyDown(m_keyMap[i][DOWN_KEY]))
          {
            m_spawnSelection[i] = EGG5;
          }
          else if (input->isKeyDown(m_keyMap[i][LEFT_KEY]))
          {
            m_spawnSelection[i] = EGG2;
          }
          else if (input->isKeyDown(m_keyMap[i][RIGHT_KEY]))
          {
            m_spawnSelection[i] = EGG4;
          }

          p_infoPanel->setSelection(m_spawnSelection[i], slot);


          if (input->isKeyReleased(m_keyMap[i][ACTION2_KEY]))
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