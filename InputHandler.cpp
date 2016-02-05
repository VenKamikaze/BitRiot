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
    m_keyMap[0][UP_KEY] = DIK_W;
	m_keyMap[0][DOWN_KEY] = DIK_S;
	m_keyMap[0][LEFT_KEY] = DIK_A;
	m_keyMap[0][RIGHT_KEY] = DIK_D;
	m_keyMap[0][ACTION1_KEY] = DIK_LCONTROL;
	m_keyMap[0][ACTION2_KEY] = DIK_LSHIFT;

	m_keyMap[1][UP_KEY] = DIK_I;
	m_keyMap[1][DOWN_KEY] = DIK_K;
	m_keyMap[1][LEFT_KEY] = DIK_J;
	m_keyMap[1][RIGHT_KEY] = DIK_L;
	m_keyMap[1][ACTION1_KEY] = DIK_LBRACKET;
	m_keyMap[1][ACTION2_KEY] = DIK_RBRACKET;

	m_keyMap[2][UP_KEY] = DIK_NUMPAD8;
	m_keyMap[2][DOWN_KEY] = DIK_NUMPAD5;
	m_keyMap[2][LEFT_KEY] = DIK_NUMPAD4;
	m_keyMap[2][RIGHT_KEY] = DIK_NUMPAD6;
	m_keyMap[2][ACTION1_KEY] = DIK_ADD;
	m_keyMap[2][ACTION2_KEY] = DIK_SUBTRACT;

	m_keyMap[3][UP_KEY] = DIK_UP;
	m_keyMap[3][DOWN_KEY] = DIK_DOWN;
	m_keyMap[3][LEFT_KEY] = DIK_LEFT;
	m_keyMap[3][RIGHT_KEY] = DIK_RIGHT;
	m_keyMap[3][ACTION1_KEY] = DIK_RCONTROL;
	m_keyMap[3][ACTION2_KEY] = DIK_RSHIFT;

}

void InputHandler::setPlayerDead(int player, bool flag)
{
	m_playerDead[player] = flag;
}

void InputHandler::processKeyboardInput()
{
	DInput::updateKeyboardState();

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
			if (DInput::keyDown(m_keyMap[i][UP_KEY]))
			{
				p_players[i]->moveInDirection(UP);
				p_players[i]->incAnimFrame();
			}
			else if (DInput::keyDown(m_keyMap[i][DOWN_KEY]))
			{
				p_players[i]->moveInDirection(DOWN);
				p_players[i]->incAnimFrame();
			}
			else if (DInput::keyDown(m_keyMap[i][LEFT_KEY]))
			{
				p_players[i]->moveInDirection(LEFT);
				p_players[i]->incAnimFrame();
			}
			else if (DInput::keyDown(m_keyMap[i][RIGHT_KEY]))
			{
				p_players[i]->moveInDirection(RIGHT);
				p_players[i]->incAnimFrame();
			}
			if (DInput::keyDown(m_keyMap[i][ACTION1_KEY]))
				action1Down[i] = true;
			else if (DInput::keyDown(m_keyMap[i][ACTION2_KEY]))
			{
				action2Down[i] = true;
				selection[i] = EGG1;
			}
		} else {
			if (action1Down[i] == true)
			{
				// get action1 + direction
				if (DInput::keyDown(m_keyMap[i][UP_KEY]))
				{
					selection[i] = BOMB;				
					p_infoPanel->setSelection(BOMB, i + 1);
				}
				else if (DInput::keyDown(m_keyMap[i][DOWN_KEY]))
				{
					selection[i] = BLOCK;
					p_infoPanel->setSelection(BLOCK, i + 1);
				}
				else if (DInput::keyDown(m_keyMap[i][LEFT_KEY]))
				{
					selection[i] = MINE;
					p_infoPanel->setSelection(MINE, i + 1);
				}
				else if (DInput::keyDown(m_keyMap[i][RIGHT_KEY]))
				{
					selection[i] = ROCKET_SPIN;
					p_infoPanel->setSelection(ROCKET_SPIN, i + 1);
				} 
				
				// for no direction press
				if (selection[i] == BLOCK) {
					p_infoPanel->setSelection(BLOCK, i + 1);
				}

				if (DInput::keyDown(m_keyMap[i][ACTION1_KEY]) == false)
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
			} else {
				// action2Down == true
				// get action2 + direction
				if (DInput::keyDown(m_keyMap[i][UP_KEY]))
				{
					selection[i] = EGG3;
				}
				else if (DInput::keyDown(m_keyMap[i][DOWN_KEY]))
				{
					selection[i] = EGG5;
				}
				else if (DInput::keyDown(m_keyMap[i][LEFT_KEY]))
				{
					selection[i] = EGG2;
				}
				else if (DInput::keyDown(m_keyMap[i][RIGHT_KEY]))
				{
					selection[i] = EGG4;
				} 
				
				p_infoPanel->setSelection(selection[i], i + 1);


				if (DInput::keyDown(m_keyMap[i][ACTION2_KEY]) == false)
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