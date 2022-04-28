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

// Note: This class no longer resembles the sample file from libRocket at all, sans the ProcessEvent method definition.
//       I've kept the MIT license as part of this anyway.

#include "EventHandlerPlayerSelect.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <string>
#include "EventManager.h"
#include "GameSettings.h"
#include "MickLogger.h"
#include "RmlUI/MickRmlUIElementUtil.h"

EventHandlerPlayerSelect::EventHandlerPlayerSelect()
{
}

EventHandlerPlayerSelect::~EventHandlerPlayerSelect()
{
}

void EventHandlerPlayerSelect::changeHumanPlayers(Rml::Event* event)
{
  int humans = std::GameSettings::getInstance()->getNumberOfHumanPlayers() + 1;
  if (humans > std::GameSettings::getInstance()->getNumberOfPlayers())
  {
    humans = 0; // wrap it back to zero human players
  }

  // reset all 
  for(int playerIndex = 0; playerIndex < std::GameSettings::getInstance()->getNumberOfPlayers(); playerIndex++)
  {
    bool isAiControlled = (playerIndex >= humans);
    std::GameSettings::getInstance()->setPlayerAI(playerIndex, isAiControlled);
  }

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), std::to_string(humans));
  }
}

void EventHandlerPlayerSelect::changeTotalPlayers(Rml::Event* event)
{
  int players = std::GameSettings::getInstance()->getNumberOfPlayers() + 1;
  if (players > std::GameSettings::MAX_PLAYERS)
  {
    players = std::GameSettings::MIN_PLAYERS; // wrap it back to the minimum
  }

  std::GameSettings::getInstance()->setNumberOfPlayers(players);

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), std::to_string(players));

    // Reset the number of human players displayed on the page too
    Rml::Element *humanPlayersElement = event->GetTargetElement()->GetOwnerDocument()->GetElementById("humanplayers");
    if(humanPlayersElement)
    {
      MickRmlUIElementUtil::replaceEndStringInTextNode(humanPlayersElement, std::to_string(std::GameSettings::getInstance()->getNumberOfHumanPlayers()));
    }
  }
}

void EventHandlerPlayerSelect::togglePlayerGender(Rml::Event* event, int playerIndex)
{
  bool newGenderIsMale = ! (std::GameSettings::getInstance()->getPlayerGender(playerIndex));
  std::GameSettings::getInstance()->setPlayerGender(playerIndex, newGenderIsMale);

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), newGenderIsMale ? "Male" : "Female");
  }
}


void EventHandlerPlayerSelect::playerSelectPageInit(Rml::ElementDocument* bodyElement)
{
  if (bodyElement == NULL)
    return;

  Rml::ElementList buttons;
  bodyElement->GetElementsByTagName(buttons, "button");
  if (buttons.size() > 0)
  {
    std::string numPlayers = (std::to_string(std::GameSettings::getInstance()->getNumberOfPlayers()));
    std::string numHumanPlayers = (std::to_string(std::GameSettings::getInstance()->getNumberOfHumanPlayers()));

    for (Rml::ElementList::iterator i = buttons.begin(); i != buttons.end(); i++)
    {
      Rml::Element* element = *i;
      if (element->GetId() == "totalplayers")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, numPlayers);
      }
      else if (element->GetId() == "humanplayers")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, numHumanPlayers);
      }
      else
      {
        if (element->GetId().find("genderplayer") == 0)
        {
          int player = atoi(element->GetId().substr(element->GetId().length() - 1, 1).c_str()) -1;
          if (std::GameSettings::getInstance()->getNumberOfPlayers() >= player)
          {
            std::string gender = (std::GameSettings::getInstance()->getPlayerGenders()->at(player)) ? "Male" : "Female";
            MickRmlUIElementUtil::appendTextToTextNode(element, gender);
          }
        }
      }
    }
  }
}

void EventHandlerPlayerSelect::ProcessEvent(Rml::Event& event, const Rml::String& value)
{
  // Called on onload of the options menu
  if (value == "changebuttons")
  {
    playerSelectPageInit(event.GetTargetElement()->GetOwnerDocument());
  }
  else if (value == "humanplayers")
  {
    changeHumanPlayers(&event);
  }
  else if (value.find("genderplayer") == 0 )
  {
    int player = atoi(event.GetTargetElement()->GetId().substr(event.GetTargetElement()->GetId().length() - 1, 1).c_str());
    if (std::GameSettings::getInstance()->getNumberOfPlayers() >= player)
    {
      togglePlayerGender(&event, player-1);
    }
  }
  else if (value == "totalplayers")
  {
    changeTotalPlayers(&event);
  }
}
