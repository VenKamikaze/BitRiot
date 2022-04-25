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

#define RMLUI_STATIC_LIB

#include "EventHandlerOptions.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include "EventManager.h"

EventHandlerOptions::EventHandlerOptions()
{
}

EventHandlerOptions::~EventHandlerOptions()
{
}

void EventHandlerOptions::changeHumanPlayers(Rml::Event* event)
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

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), std::to_string(humans));
  }
}

void EventHandlerOptions::changeTotalPlayers(Rml::Event* event)
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

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), std::to_string(players));
  }
}

void EventHandlerOptions::changeBlockTilePercentage(Rml::Event* event)
{
  int blockSpawnPct = std::GameSettings::getInstance()->getBlockSpawnPercentage();
  if (blockSpawnPct >= std::GameSettings::MAX_BLOCK_PCT)
  {
    blockSpawnPct = std::GameSettings::MIN_BLOCK_PCT;
  }
  else
  {
    blockSpawnPct += 5;
  }

  std::GameSettings::getInstance()->setBlockSpawnPercentage(blockSpawnPct);
  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), std::to_string(blockSpawnPct));
  }
}

void EventHandlerOptions::togglePlayerGender(Rml::Event* event, int playerIndex)
{
  bool newGenderIsMale = ! (std::GameSettings::getInstance()->getPlayerGender(playerIndex));
  std::GameSettings::getInstance()->setPlayerGender(playerIndex, newGenderIsMale);

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), newGenderIsMale ? "Male" : "Female");
  }
}


void EventHandlerOptions::OptionsPageInit(Rml::ElementDocument* bodyElement)
{
  if (bodyElement == NULL)
    return;

  Rml::ElementList buttons;
  bodyElement->GetElementsByTagName(buttons, "button");
  if (buttons.size() > 0)
  {
    std::string numPlayers = (std::to_string(std::GameSettings::getInstance()->getNumberOfPlayers()));
    std::string numHumanPlayers = (std::to_string(std::GameSettings::getInstance()->getNumberOfHumanPlayers()));
    std::string blockTilePct = (std::to_string(std::GameSettings::getInstance()->getBlockSpawnPercentage()));
    std::string mapWidth = (std::to_string(std::GameSettings::getInstance()->getMapWidth()));
    std::string mapHeight = (std::to_string(std::GameSettings::getInstance()->getMapHeight()));

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
      else if (element->GetId() == "blocktilepercent")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, blockTilePct);
      }
      else if (element->GetId() == "mapwidth")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, mapWidth);
      }
      else if (element->GetId() == "mapheight")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, mapHeight);
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

void EventHandlerOptions::ProcessEvent(Rml::Event& event, const Rml::String& value)
{
  // Called on onload of the options menu
  if (value == "changebuttons")
  {
    OptionsPageInit(event.GetTargetElement()->GetOwnerDocument());
  }
  else if (value == "humanplayers")
  {
    changeHumanPlayers(&event);
  }
  else if (value == "blocktilepercent")
  {
    changeBlockTilePercentage(&event);
  }
  else if (value == "blocktilepercent")
  {
    changeBlockTilePercentage(&event);
  }
  else if (value.find("genderplayer") == 0 )
  {
    int player = atoi(event.GetTargetElement()->GetId().substr(event.GetTargetElement()->GetId().length() - 1, 1).c_str());
    // TODO we should conditionally display these buttons when toggling total number of players
    //   instead of having to check the bounds of the total players array.
    if (std::GameSettings::getInstance()->getNumberOfPlayers() >= player)
    {
      togglePlayerGender(&event, player-1);
    }
  }
  else if (value == "totalplayers")
  {
    // TODO, FIXME causes out of bounds check to fail - presumably in GameEngine?
    //changeTotalPlayers(&event);
    //changeHumanPlayers(NULL);
  }
}
