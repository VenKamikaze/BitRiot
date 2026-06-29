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
#include "MickSDLRenderer.h"
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

// NPC Spawner toggle. The purple CPU spawning pool re-reads this live each tick,
// so it applies immediately and persists across "Play again". Power-ups still
// spawn while it is Off; only enemy eggs are suppressed.
void EventHandlerOptions::toggleNpcSpawner(Rml::Event* event)
{
  bool enabled = !std::GameSettings::getInstance()->isNpcSpawnerEnabled();
  std::GameSettings::getInstance()->setNpcSpawnerEnabled(enabled);
  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), enabled ? "On" : "Off");
  }
}

// The live SDL window is the single source of truth for fullscreen state.
bool EventHandlerOptions::isFullscreen()
{
  SDL_Window* window = MickSDLRenderer::getInstance()->getWindowHandle();
  return window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void EventHandlerOptions::toggleFullscreen(Rml::Event* event)
{
  SDL_Window* window = MickSDLRenderer::getInstance()->getWindowHandle();
  if (window == nullptr)
    return;

  bool goFullscreen = !isFullscreen();
  // FULLSCREEN_DESKTOP keeps the desktop resolution; the renderer scales the
  // back buffer to the window via SDL_RenderCopy, so no texture recreation is
  // needed. The RmlUi menu rescales itself in MenuSDLRenderer::handleWindowResize.
  if (SDL_SetWindowFullscreen(window, goFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
  {
    std::MickLogger::getInstance()->error(this, std::string("SDL_SetWindowFullscreen failed: ").append(SDL_GetError()));
    return;
  }

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), goFullscreen ? "On" : "Off");
  }
}

// The renderer holds the authoritative preserve-aspect flag (it owns the game
// blit). We cannot use SDL_RenderSetLogicalSize because that would also transform
// the RmlUi menu (which shares the SDL_Renderer) and push it off-screen; instead
// the renderer letterboxes only the game's RenderCopy dest rect.
bool EventHandlerOptions::isPreserveAspect()
{
  return MickSDLRenderer::getInstance()->isPreserveAspect();
}

void EventHandlerOptions::togglePreserveAspect(Rml::Event* event)
{
  bool preserve = !isPreserveAspect();
  MickSDLRenderer::getInstance()->setPreserveAspect(preserve);

  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), preserve ? "On" : "Off");
  }
}

void EventHandlerOptions::changeMapWidth(Rml::Event* event)
{
  int width = std::GameSettings::getInstance()->getMapWidth();
  // Step by 2 (keep odd); wrap MAX -> MIN. Applied at GAME_INIT, not live.
  width = (width >= std::GameSettings::MAX_MAP_WIDTH)
            ? std::GameSettings::MIN_MAP_WIDTH
            : width + std::GameSettings::MAP_SIZE_STEP;
  std::GameSettings::getInstance()->setMapWidth(width);
  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), std::to_string(width));
  }
}

void EventHandlerOptions::changeMapHeight(Rml::Event* event)
{
  int height = std::GameSettings::getInstance()->getMapHeight();
  height = (height >= std::GameSettings::MAX_MAP_HEIGHT)
             ? std::GameSettings::MIN_MAP_HEIGHT
             : height + std::GameSettings::MAP_SIZE_STEP;
  std::GameSettings::getInstance()->setMapHeight(height);
  if (event)
  {
    MickRmlUIElementUtil::replaceEndStringInTextNode(event->GetTargetElement(), std::to_string(height));
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
      else if (element->GetId() == "npcspawner")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, std::GameSettings::getInstance()->isNpcSpawnerEnabled() ? "On" : "Off");
      }
      else if (element->GetId() == "mapwidth")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, mapWidth);
      }
      else if (element->GetId() == "mapheight")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, mapHeight);
      }
      else if (element->GetId() == "fullscreen")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, isFullscreen() ? "On" : "Off");
      }
      else if (element->GetId() == "preserveaspect")
      {
        MickRmlUIElementUtil::appendTextToTextNode(element, isPreserveAspect() ? "On" : "Off");
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
  else if (value == "npcspawner")
  {
    toggleNpcSpawner(&event);
  }
  else if (value == "fullscreen")
  {
    toggleFullscreen(&event);
  }
  else if (value == "preserveaspect")
  {
    togglePreserveAspect(&event);
  }
  else if (value == "mapwidth")
  {
    changeMapWidth(&event);
  }
  else if (value == "mapheight")
  {
    changeMapHeight(&event);
  }
}
