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

#include "EventHandlerOptions.h"
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

EventHandlerOptions::EventHandlerOptions()
{
}

EventHandlerOptions::~EventHandlerOptions()
{
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

void EventHandlerOptions::OptionsPageInit(Rml::ElementDocument* bodyElement)
{
  if (bodyElement == NULL)
    return;

  Rml::ElementList buttons;
  bodyElement->GetElementsByTagName(buttons, "button");
  if (buttons.size() > 0)
  {
    std::string blockTilePct = (std::to_string(std::GameSettings::getInstance()->getBlockSpawnPercentage()));
    std::string mapWidth = (std::to_string(std::GameSettings::getInstance()->getMapWidth()));
    std::string mapHeight = (std::to_string(std::GameSettings::getInstance()->getMapHeight()));

    for (Rml::ElementList::iterator i = buttons.begin(); i != buttons.end(); i++)
    {
      Rml::Element* element = *i;
      if (element->GetId() == "blocktilepercent")
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
  else if (value == "blocktilepercent")
  {
    changeBlockTilePercentage(&event);
  }
  else if (value == "blocktilepercent")
  {
    changeBlockTilePercentage(&event);
  }
}
