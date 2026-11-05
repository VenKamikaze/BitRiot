/*
 * MenuRenderer.h
 *
 *  Created on: Apr 2, 2022
 *      Author: msaun
 */

#ifndef MENUSDLRENDERER_H_
#define MENUSDLRENDERER_H_

#include "InputHandler.h"
#include <RmlUi/Core/ElementDocument.h>
#include <SDL2/SDL_render.h>

#include "RmlUI/binders/PlayersSelectBinder.h"
#include "SDL_keycode.h"
#include "SDL_joystick.h"
#include "SDL_pixels.h"
#include "RuntimeException.h"

#include "SDL_gamecontroller.h"
#include "SDL_keycode.h"
#include <SDL2/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Input.h>
#include <memory>
#ifndef NDEBUG
#include <RmlUi/Debugger/Debugger.h>
#endif

#include "RmlUI/glue/SystemInterfaceSDL2.h"
#include "RmlUI/glue/RenderInterfaceSDL2.h"
#include "RmlUI/glue/ShellFileInterface.h"


#include "MickLogger.h"
#include "PlayerCharacterEntity.h"
#include "GameSettings.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/events/EventInstancer.h"
#include "RmlUI/events/EventManager.h"
#include "RmlUI/events/EventHandlerOptions.h"
#include "RmlUI/events/EventHandlerPlayerSelect.h"
#include "RmlUI/binders/ScoreBoardBinder.h"
#include <iostream>
#include <stdexcept>
#include <string>

struct ControllerPlayerSelect {
  Rml::Element *focussedElement;
  
  bool isReady = false;
  int controllerId = -1; // will be >= 0 if controller input (action1) is detected.
  bool isKeyboardId = -1; // will be >= 0 if keyboard input (action1) is detected.
};


class MenuSDLRenderer
{
  public:
    MenuSDLRenderer(SDL_Renderer *renderer, SDL_Window *screen, shared_ptr<InputHandler>);
    void loadScoreBoard(shared_ptr<PlayerCharacterEntity> winner, vector<shared_ptr<PlayerCharacterEntity>> allPlayers, int gameTotalLength);
    void clearScoreBoard();
    // Closes every menu page, as the Start button does. Used by GameEngine::startAllAiMatch
    // (the web build's test hook), which starts a round without going through the menus.
    void closeAllMenus();
    bool showMenu();

    // The scoreboard's "Play Again" button on a machine that cannot start the next round:
    // relabels it (an empty label restores "Play Again") and greys it out when disabled.
    void setScoreboardPlayAgain(const std::string& label, bool enabled);
#ifdef BITRIOT_NETPLAY
    // -- netplay (briefs 07 and 08) ---------------------------------------------------
    // The host and join forms (nethost.rml / netjoin.rml); the main menu reaches them with "open".
    // The lobby page: closes every page and opens netlobby.rml.
    void showNetLobby();
    // Opens the main menu page (after a network game is left).
    void showMainMenu();
    // Opens a page by name, closing the others (the join form after a failed join).
    void showPage(const std::string& file);
    bool netLobbyOpen() const;
    // The lobby document, or nullptr when it is not open; the NetLobbyBinder fills it.
    Rml::ElementDocument* netLobbyDocument() const;
    // The scoreboard of a network game: "Play Again" becomes the host's "Next round" (or a peer's disabled
    // "Waiting for the host..."), and the host also gets "Back to lobby".
    void setScoreboardNet(bool host, const std::string& playAgainLabel, bool playAgainEnabled, bool backToLobbyEnabled);
    // The in-game overlay (netoverlay.rml): who the match is waiting for, who left, a desync, a status line.
    struct NetOverlay
    {
      std::string stall;                  // "" = hidden
      std::vector<std::string> toasts;
      std::string desync;                 // "" = hidden
      std::string join;                   // "" = hidden: "Joining... catching up 43%", a late joiner's banner (brief 13)
      std::string status;
    };
    // The error line of the open host or join form.
    void setFormError(const std::string& text);
    void setNetOverlay(const NetOverlay& overlay);
    void hideNetOverlay();
    // Draws the overlay over what was just presented to the renderer (call after pushCpuBufferToHardwareBuffer).
    void renderNetOverlay();
#endif
    virtual ~MenuSDLRenderer();

  private:
    void handleWindowResize();
    void initGui(SDL_Renderer* renderer, SDL_Window *screen);
    int playerJoin();
    int getTabIndex(Rml::Element* node);
    Rml::Element* getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex);
    Rml::Context* context;
    Rml::ElementDocument* loadMenu(std::string menuRmlFile, bool loadAndShow = true);
    SDL_Window *m_screen;
    SDL_Renderer *m_renderer;
    ScoreBoardBinder *m_scoreBinder = nullptr;
    unique_ptr<PlayersSelectBinder> m_playersBinder = nullptr;
    shared_ptr<InputHandler> m_inputHandler = nullptr;
    std::map<int, ControllerPlayerSelect> playerIdToFocussedElement;
    shared_ptr<EventHandlerPlayerSelect> m_eHandlerPlayerSelect;
    bool m_textInputOn = false;   // SDL_StartTextInput is in force (a text field has the focus)
#ifdef BITRIOT_NETPLAY
    Rml::ElementDocument* m_overlay = nullptr;
#endif
};

#endif /* MENUSDLRENDERER_H_ */
