/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "EventHandlerOptions.h"
#include <Rocket/Core/ElementDocument.h>
#include <Rocket/Core/ElementUtilities.h>
#include <Rocket/Core/Event.h>
#include <Rocket/Controls/ElementFormControlInput.h>
#include "EventManager.h"

EventHandlerOptions::EventHandlerOptions()
{
}

EventHandlerOptions::~EventHandlerOptions()
{
}

void EventHandlerOptions::changeHumanPlayers(Rocket::Core::Event* event)
{
	int humans = std::GameSettings::getInstance()->getNumberOfHumanPlayers();
	if (humans >= std::GameSettings::getInstance()->getNumberOfPlayers())
	{
		humans = 0;
		for (int i = 0; i < std::GameSettings::getInstance()->getNumberOfPlayers(); i++)
		{
			std::GameSettings::getInstance()->setPlayerAI(i, true);
		}
	}
	else
	{
		++humans;
		std::GameSettings::getInstance()->setPlayerAI(humans - 1, false);
	}

	if(event)
	{
		MickRocketElementUtil::replaceEndCharInTextNode(event->GetTargetElement(), std::to_string(humans));
	}
}

void EventHandlerOptions::changeTotalPlayers(Rocket::Core::Event* event)
{
	int players = std::GameSettings::getInstance()->getNumberOfPlayers();
	if (players >= std::GameSettings::MAX_PLAYERS)
	{
		players = 1;
		std::GameSettings::getInstance()->setNumberOfPlayers(players);
	}
	else
	{
		++players;
		std::GameSettings::getInstance()->setNumberOfPlayers(players);
		std::GameSettings::getInstance()->setPlayerAI(players - 1, true);
	}

	if(event)
	{
		MickRocketElementUtil::replaceEndCharInTextNode(event->GetTargetElement(), std::to_string(players));
	}
}

void EventHandlerOptions::ProcessEvent(Rocket::Core::Event& event, const Rocket::Core::String& value)
{
	printf("EventHandlerOptions got event %s\n", value.CString());

	// Called on onload of the options menu
	if(value == "changebuttons")
	{
		Rocket::Core::ElementDocument* bodyElement = event.GetTargetElement()->GetOwnerDocument();
		if(bodyElement == NULL)
			return;

		Rocket::Core::ElementList buttons;
		bodyElement->GetElementsByTagName(buttons, "button");
		if(buttons.size() > 0)
		{
          std::string numPlayers = (std::to_string( std::GameSettings::getInstance()->getNumberOfPlayers()));
          std::string numHumanPlayers = (std::to_string( std::GameSettings::getInstance()->getNumberOfHumanPlayers()));
          std::string blockTilePct = (std::to_string( std::GameSettings::getInstance()->getBlockSpawnPercentage()));

          for (Rocket::Core::ElementList::iterator i = buttons.begin(); i != buttons.end(); i++)
		  {
			Rocket::Core::Element* element = *i;
		    if(element->GetId() == "totalplayers")
		    {
			  MickRocketElementUtil::appendTextToTextNode(element, numPlayers);
		    }
		    else if(element->GetId() == "humanplayers")
		    {
		      MickRocketElementUtil::appendTextToTextNode(element, numHumanPlayers);
		    }
		    else if(element->GetId() == "blocktilepercent")
		    {
		      MickRocketElementUtil::appendTextToTextNode(element, blockTilePct);
		    }
		    else
		    {
		      if(element->GetId().Find("genderplayer") == 0)
		      {
		        int player = atoi(element->GetId().Substring(element->GetId().Length() -1, 1).CString());
		        if (std::GameSettings::getInstance()->getNumberOfPlayers() >= player)
		        {
			      std::string gender = ( std::GameSettings::getInstance()->getPlayerGenders().at(player) ) ? "Male" : "Female";
			      MickRocketElementUtil::appendTextToTextNode(element, gender);
		        }
		      }
		    }
		  }
		}
	}
	else if (value == "humanplayers")
	{
		changeHumanPlayers(&event);
	}
	else if (value == "totalplayers")
	{
		// TODO, FIXME causes out of bounds check to fail - presumably in GameEngine?
		//changeTotalPlayers(&event);
		//changeHumanPlayers(NULL);
	}
}
