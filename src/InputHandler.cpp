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
  for(const auto& player: playerCharacters)
  {
    m_keyMap[player->getID()] = m_availableKeySets.front();
    m_availableKeySets.erase(m_availableKeySets.begin());
    m_action1Down.insert(std::pair<int, bool>(player->getID(), false));
    m_action2Down.insert(std::pair<int, bool>(player->getID(), false));
    m_spawnSelection.insert(std::pair<int, EntityType>(player->getID(), BLOCK));
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

//void InputHandler::setPlayerDead(int player, bool flag)
//{
//  m_playerDead.at(player) = flag;
//}

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
  shared_ptr<MickBaseInput> input = MickSDLInput::getInstance(shared_from_this());
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
      if ((!player) || (!player->isAlive()) || (player->isControlledByAI()) )
      {
        continue;
      }

      int i = player->getID();

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
            p_infoPanel->setSelection(BOMB, i + 1);
          }
          else if (input->isKeyDown(m_keyMap[i][DOWN_KEY]))
          {
            m_spawnSelection[i] = BLOCK;
            p_infoPanel->setSelection(BLOCK, i + 1);
          }
          else if (input->isKeyDown(m_keyMap[i][LEFT_KEY]))
          {
            m_spawnSelection[i] = MINE;
            p_infoPanel->setSelection(MINE, i + 1);
          }
          else if (input->isKeyDown(m_keyMap[i][RIGHT_KEY]))
          {
            m_spawnSelection[i] = ROCKET_SPIN;
            p_infoPanel->setSelection(ROCKET_SPIN, i + 1);
          }

          // for no direction press
          if (m_spawnSelection[i] == BLOCK)
          {
            p_infoPanel->setSelection(BLOCK, i + 1);
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
            p_infoPanel->setSelection(PLAYER_CHARACTER, i + 1);
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

          p_infoPanel->setSelection(m_spawnSelection[i], i + 1);


          if (input->isKeyReleased(m_keyMap[i][ACTION2_KEY]))
          {
            // action2 released this frame, try to place egg
            if (p_dynamicMap->tileHasStaticEntity(player->getTileX(), player->getTileY()) == false)
            {
              player->createEntity(m_spawnSelection[i]);
            }

            m_action2Down[i] = false;
            m_spawnSelection[i] = BLOCK;
            p_infoPanel->setSelection(PLAYER_CHARACTER, i + 1);
          }

        }
      } // end if action1 && action2 down
    }
  }
}
