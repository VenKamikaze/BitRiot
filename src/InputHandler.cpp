#include "InputHandler.h"


InputHandler::InputHandler()
{
	setKeys();
}

InputHandler::~InputHandler()
{

}

void InputHandler::setPointers(int numPlayers, PlayerCharacterEntity * player1,
		PlayerCharacterEntity * player2, PlayerCharacterEntity * player3,
		PlayerCharacterEntity * player4, DynamicMap * dynamicMap, InfoPanel * panel)
{
	m_numPlayers = numPlayers;
	p_players[0] = player1;
	p_players[1] = player2;
	p_players[2] = player3;
	p_players[3] = player4;
	p_dynamicMap = dynamicMap;
	p_infoPanel = panel;
}

void InputHandler::setKeys()
{
	m_keyMap[0][UP_KEY] = KEY_w;
	m_keyMap[0][DOWN_KEY] = KEY_s;
	m_keyMap[0][LEFT_KEY] = KEY_a;
	m_keyMap[0][RIGHT_KEY] = KEY_d;
	m_keyMap[0][ACTION1_KEY] = KEY_LCTRL;
	m_keyMap[0][ACTION2_KEY] = KEY_LSHIFT;

	m_keyMap[1][UP_KEY] = KEY_i;
	m_keyMap[1][DOWN_KEY] = KEY_k;
	m_keyMap[1][LEFT_KEY] = KEY_j;
	m_keyMap[1][RIGHT_KEY] = KEY_l;
	m_keyMap[1][ACTION1_KEY] = KEY_LEFTBRACKET;
	m_keyMap[1][ACTION2_KEY] = KEY_RIGHTBRACKET;

	m_keyMap[2][UP_KEY] = KEY_KP8;
	m_keyMap[2][DOWN_KEY] = KEY_KP5;
	m_keyMap[2][LEFT_KEY] = KEY_KP4;
	m_keyMap[2][RIGHT_KEY] = KEY_KP6;
	m_keyMap[2][ACTION1_KEY] = KEY_KP_PLUS;
	m_keyMap[2][ACTION2_KEY] = KEY_KP_MINUS;

	m_keyMap[3][UP_KEY] = KEY_UP;
	m_keyMap[3][DOWN_KEY] =  KEY_DOWN;
	m_keyMap[3][LEFT_KEY] =  KEY_LEFT;
	m_keyMap[3][RIGHT_KEY] =  KEY_RIGHT;
	m_keyMap[3][ACTION1_KEY] =  KEY_RCTRL;
	m_keyMap[3][ACTION2_KEY] =  KEY_RSHIFT;
}

void InputHandler::setPlayerDead(int player, bool flag)
{
	m_playerDead[player] = flag;
}

void InputHandler::processKeyboardInput(/*SDL_Event event*/)
{
	MickBaseInput* input = MickSDLInput::getInstance();
	input->updateEventQueue();

	if(input->doQuit())
	{
		cout << "Got QUIT!\n";
		InputException e;
		e.setType(GOT_QUIT);
		throw e;
	}
	else if(input->isAKeyDown() || input->isAKeyReleased())
	{
		for (int i = 0; i < m_numPlayers; i++)
		{
			if (p_players[i] == NULL)
				continue;
			if (m_playerDead[i])
				continue;
			if (p_players[i]->isControlledByAI())
				continue;

			static bool action1Down[4] = { false, false, false, false };
			static bool action2Down[4] = { false, false, false, false };
			static EntityType selection[4] = { BLOCK, BLOCK, BLOCK, BLOCK };

			if (action1Down[i] == false && action2Down[i] == false)
			{
				if (input->isKeyDown(m_keyMap[i][UP_KEY]))
				{
					//cout << "UP: KEY IS DOWN!\n";
					p_players[i]->moveInDirection(UP);
					p_players[i]->incAnimFrame();
				}
				else if (input->isKeyDown(m_keyMap[i][DOWN_KEY]))
				{
					p_players[i]->moveInDirection(DOWN);
					p_players[i]->incAnimFrame();
				}
				else if (input->isKeyDown(m_keyMap[i][LEFT_KEY]))
				{
					p_players[i]->moveInDirection(LEFT);
					p_players[i]->incAnimFrame();
				}
				else if (input->isKeyDown(m_keyMap[i][RIGHT_KEY]))
				{
					p_players[i]->moveInDirection(RIGHT);
					p_players[i]->incAnimFrame();
				}
				if (input->isKeyDown(m_keyMap[i][ACTION1_KEY]))
					action1Down[i] = true;
				else if (input->isKeyDown(m_keyMap[i][ACTION2_KEY]))
				{
					action2Down[i] = true;
					selection[i] = EGG1;
				}
			}
      else // action1Down == true || action2Down == true
      {
				if (action1Down[i] == true)
				{
					// get action1 + direction
					if (input->isKeyDown(m_keyMap[i][UP_KEY]))
					{
						selection[i] = BOMB;
						p_infoPanel->setSelection(BOMB, i + 1);
					}
					else if (input->isKeyDown(m_keyMap[i][DOWN_KEY]))
					{
						selection[i] = BLOCK;
						p_infoPanel->setSelection(BLOCK, i + 1);
					}
					else if (input->isKeyDown(m_keyMap[i][LEFT_KEY]))
					{
						selection[i] = MINE;
						p_infoPanel->setSelection(MINE, i + 1);
					}
					else if (input->isKeyDown(m_keyMap[i][RIGHT_KEY]))
					{
						selection[i] = ROCKET_SPIN;
						p_infoPanel->setSelection(ROCKET_SPIN, i + 1);
					}

					// for no direction press
					if (selection[i] == BLOCK) {
						p_infoPanel->setSelection(BLOCK, i + 1);
					}

					if (input->isKeyReleased(m_keyMap[i][ACTION1_KEY]))
					{
						// control released this frame, try to place item
						if (p_dynamicMap->tileHasStaticEntity(p_players[i]->getTileX(), p_players[i]->getTileY()) == false)
						{
							// TO DO - if trying to place block, check there is an exit
							p_players[i]->createEntity(selection[i]);
						}

						action1Down[i] = false;
						selection[i] = BLOCK;
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
						selection[i] = EGG3;
					}
					else if (input->isKeyDown(m_keyMap[i][DOWN_KEY]))
					{
						selection[i] = EGG5;
					}
					else if (input->isKeyDown(m_keyMap[i][LEFT_KEY]))
					{
						selection[i] = EGG2;
					}
					else if (input->isKeyDown(m_keyMap[i][RIGHT_KEY]))
					{
						selection[i] = EGG4;
					}

					p_infoPanel->setSelection(selection[i], i + 1);


					if (input->isKeyReleased(m_keyMap[i][ACTION2_KEY]))
					{
						// action2 released this frame, try to place egg
						if (p_dynamicMap->tileHasStaticEntity(p_players[i]->getTileX(), p_players[i]->getTileY()) == false)
						{
							p_players[i]->createEntity(selection[i]);
						}

						action2Down[i] = false;
						selection[i] = BLOCK;
						p_infoPanel->setSelection(PLAYER_CHARACTER, i + 1);
					}

				}
			} // end if action1 && action2 down
		}
	}
}
