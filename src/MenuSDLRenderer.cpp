// implementation of Map.h

#include "MenuSDLRenderer.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/events/EventHandlerPlayerSelect.h"
#ifdef BITRIOT_NETPLAY
#include "RmlUI/binders/NetLobbyBinder.h"
#include "RmlUI/events/EventHandlerNetLobby.h"
#endif
#include "SDL_events.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <tuple>


using namespace std;

MenuSDLRenderer::MenuSDLRenderer(SDL_Renderer* renderer, SDL_Window* screen, shared_ptr<InputHandler> inputHandler)
{
  initGui(renderer, screen);
  m_inputHandler = inputHandler;
}

Rml::ElementDocument* MenuSDLRenderer::loadMenu(std::string menuRmlFile, bool loadAndShow)
{
  Rml::ElementDocument *doc = EventManager::getInstance()->LoadWindow(menuRmlFile.c_str(), loadAndShow);
  if (! doc)
  {
    MickLogger::getInstance()->error(this, string("Error loading ").append(menuRmlFile));
    throw std::runtime_error(std::string("Menu RML was not found"));
  }
  return doc;
}

void MenuSDLRenderer::initGui(SDL_Renderer* renderer, SDL_Window *screen)
{
  int windowWidth, windowHeight;
  SDL_GetWindowSize(screen, &windowWidth, &windowHeight);
  //not initialised on start: SDL_GetRendererOutputSize(m_renderer, &windowWidth, &windowHeight);
  MickLogger::getInstance()->debug(this, std::string("MenuRenderer::init window wxh: ").append(std::to_string(windowWidth)).append("x").append(std::to_string(windowHeight)) );
  RmlUiSDL2Renderer* rmluiRenderGlue = new RmlUiSDL2Renderer(renderer, screen);
  RmlUiSDL2SystemInterface* rmluiSystemGlue = new RmlUiSDL2SystemInterface();
  ShellFileInterface* rmluiFileGlue = new ShellFileInterface("assets/menu/");

  Rml::SetFileInterface(rmluiFileGlue);
  Rml::SetRenderInterface(rmluiRenderGlue);
  Rml::SetSystemInterface(rmluiSystemGlue);

  if(!Rml::Initialise())
  {
    throw std::runtime_error(std::string("Unable to init RmlUi."));
  }

  Rml::LoadFontFace("assets/fonts/FreeMono.ttf");
  // These appear to be non-free
  //Rml::LoadFontFace("assets/fonts/Delicious-Bold.otf");
  //Rml::LoadFontFace("assets/fonts/Delicious-BoldItalic.otf");
  //Rml::LoadFontFace("assets/fonts/Delicious-Italic.otf");
  //Rml::LoadFontFace("assets/fonts/Delicious-Roman.otf");

  context = Rml::CreateContext("default",
                                   Rml::Vector2i(windowWidth, windowHeight));

#ifndef NDEBUG
  Rml::Debugger::Initialise(context);
#endif

  // Initialise the event instancer and handlers.
  EventInstancer* event_instancer = new EventInstancer();
  Rml::Factory::RegisterEventListenerInstancer(event_instancer);

  m_eHandlerPlayerSelect = make_shared<EventHandlerPlayerSelect>();

  EventManager::getInstance()->RegisterEventHandler("gameoptions.rml", new EventHandlerOptions());
  EventManager::getInstance()->RegisterEventHandler("playerselect.rml", m_eHandlerPlayerSelect.get());
#ifdef BITRIOT_NETPLAY
  EventManager::getInstance()->RegisterEventHandler(EventHandlerNetLobby::RML_FILENAME, new EventHandlerNetLobby());
  EventManager::getInstance()->RegisterEventHandler(EventHandlerNetForm::HOST_FILENAME, new EventHandlerNetForm());
  EventManager::getInstance()->RegisterEventHandler(EventHandlerNetForm::JOIN_FILENAME, new EventHandlerNetForm());
  EventManager::getInstance()->RegisterEventHandler(EventHandlerNetForm::RELAY_HOST_FILENAME, new EventHandlerNetForm());
  EventManager::getInstance()->RegisterEventHandler(EventHandlerNetForm::RELAY_JOIN_FILENAME, new EventHandlerNetForm());

  // The last name, address and port live in a small file in the user's preferences folder, which is only looked up (and
  // so only created) when an online page first needs it.
  EventHandlerNetForm::setPrefsPathProvider([]() -> std::string {
    std::string path;
    if (char* pref = SDL_GetPrefPath("BitRiot", "BitRiot"))
    {
      path = std::string(pref) + "netplay.txt";
      SDL_free(pref);
    }
    return path;
  });

  // The main menu gets its online entry here and not in mainmenu.rml, so a build without netplay shows exactly the menu it always
  // had. One entry, "Online game", that opens netmenu.rml (brief 12: the main menu would have had seven lines, more than the
  // smallest window holds); the page lists the ways in: Host via relay and Join by room code, and on a native build the direct UDP
  // pair brief 08 added. A browser cannot open a UDP socket, so there those two are taken out of the page.
  EventManager::SetDocumentLoadedHook([](Rml::ElementDocument& doc, const Rml::String& name) {
    if (name == "netmenu.rml")
    {
#ifdef __EMSCRIPTEN__
      for (const char* id : {"nethost", "netjoin"})
      {
        if (Rml::Element* e = doc.GetElementById(id))
        {
          // The <br /> that follows the button goes with it.
          Rml::Element* parent = e->GetParentNode();
          Rml::Element* after = e->GetNextSibling();
          if (parent && after && after->GetTagName() == "br")
          {
            parent->RemoveChild(after);
          }
          if (parent)
          {
            parent->RemoveChild(e);
          }
        }
      }
      if (Rml::Element* back = doc.GetElementById("netback"))
      {
        back->SetAttribute("tabindex", "3");
      }
#endif
      return;
    }
    if (name != "mainmenu.rml")
    {
      return;
    }
    // The button goes in after "Play", into the list mainmenu.rml has: the tab indexes of what follows move up by one.
    Rml::Element* play = doc.GetElementById("quickstart");
    Rml::Element* menu = play ? play->GetParentNode() : nullptr;
    if (!menu)
    {
      return;
    }
    Rml::ElementList buttons;
    doc.GetElementsByTagName(buttons, "button");
    for (Rml::Element* b : buttons)
    {
      const int index = MickRmlUIElementUtil::getTabIndex(b);
      if (index >= 2)
      {
        b->SetAttribute("tabindex", std::to_string(index + 1));
      }
    }
    // Inserted before the first button after Play (Rml has no insert-after: the buttons are followed by <br />).
    Rml::Element* before = nullptr;
    for (int i = 0; i < menu->GetNumChildren(); i++)
    {
      if (menu->GetChild(i) == play)
      {
        // Play, its <br />, then the next child.
        before = i + 2 < menu->GetNumChildren() ? menu->GetChild(i + 2) : nullptr;
        break;
      }
    }
    Rml::ElementPtr b = doc.CreateElement("button");
    b->SetId("netonline");
    b->SetClassNames("tabbed");
    b->SetAttribute("tabindex", "2");
    b->SetAttribute("onclick", "open netmenu.rml");
    b->SetInnerRML("Online game");
    menu->InsertBefore(std::move(b), before);
    menu->InsertBefore(doc.CreateElement("br"), before);
  });
#endif

  // SDL2 turns text input on at video init on the desktop; it is wanted only while a text field has the focus.
  SDL_StopTextInput();

  m_playersBinder = make_unique<PlayersSelectBinder>(context);

  loadMenu("mainmenu.rml");
  m_renderer = renderer;
  m_screen = screen;
}

void MenuSDLRenderer::loadScoreBoard(shared_ptr<PlayerCharacterEntity> winner, vector<shared_ptr<PlayerCharacterEntity>> allPlayers, int gameTotalLength)
{
  m_scoreBinder = new ScoreBoardBinder(context, winner, allPlayers, gameTotalLength);
  // The scoreboard now lists every player grouped by team (with a "Team N wins"
  // / draw headline), so there is no separate winner-only block to hide.
  loadMenu("scoreboard.rml", true);
}

void MenuSDLRenderer::clearScoreBoard()
{
  if(m_scoreBinder && context)
  {
    m_scoreBinder->RemoveDataBinding(context);
    delete m_scoreBinder;
    m_scoreBinder = nullptr;
  }
}

void MenuSDLRenderer::closeAllMenus()
{
#ifdef BITRIOT_NETPLAY
  m_overlay = nullptr;
#endif
  context->UnloadAllDocuments();
}

namespace
{
Rml::ElementDocument* findDocument(Rml::Context* context, const std::string& file)
{
  for (int i = 0; i < context->GetNumDocuments(); i++)
  {
    Rml::ElementDocument* doc = context->GetDocument(i);
    if (doc && doc->GetSourceURL() == file)
    {
      return doc;
    }
  }
  return nullptr;
}

// An <input type="text"> (the only text field the menus have).
bool isTextField(Rml::Element* e)
{
  return e != nullptr && e->GetTagName() == "input" && e->GetAttribute<Rml::String>("type", "text") == "text";
}

// Plain text to RML: escaped, with line breaks.
std::string textToRml(const std::string& text)
{
  std::string out;
  for (char c : text)
  {
    switch (c)
    {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '\n': out += "<br />"; break;
      default: out += c; break;
    }
  }
  return out;
}
} // namespace

#ifdef BITRIOT_NETPLAY
void MenuSDLRenderer::showNetLobby()
{
  m_overlay = nullptr;
  context->UnloadAllDocuments();
  loadMenu(EventHandlerNetLobby::RML_FILENAME, true);
}

void MenuSDLRenderer::showMainMenu()
{
  m_overlay = nullptr;
  context->UnloadAllDocuments();
  loadMenu("mainmenu.rml", true);
}

void MenuSDLRenderer::showPage(const std::string& file)
{
  m_overlay = nullptr;
  context->UnloadAllDocuments();
  loadMenu(file, true);
}

bool MenuSDLRenderer::netLobbyOpen() const
{
  return findDocument(context, EventHandlerNetLobby::RML_FILENAME) != nullptr;
}

Rml::ElementDocument* MenuSDLRenderer::netLobbyDocument() const
{
  return findDocument(context, EventHandlerNetLobby::RML_FILENAME);
}

void MenuSDLRenderer::setScoreboardNet(bool host, const std::string& playAgainLabel, bool playAgainEnabled, bool backToLobbyEnabled)
{
  setScoreboardPlayAgain(playAgainLabel, playAgainEnabled);
  Rml::ElementDocument* doc = findDocument(context, "scoreboard.rml");
  if (!doc)
  {
    return;
  }
  if (Rml::Element* e = doc->GetElementById("lobbyline"))
  {
    e->SetProperty("display", host ? "block" : "none");
  }
  if (Rml::Element* e = doc->GetElementById("backtolobby"))
  {
    e->SetProperty("color", backToLobbyEnabled ? "#ffffff" : "#777777");
  }
}

void MenuSDLRenderer::setFormError(const std::string& text)
{
  for (const char* file : {EventHandlerNetForm::HOST_FILENAME, EventHandlerNetForm::JOIN_FILENAME})
  {
    if (Rml::ElementDocument* doc = findDocument(context, file))
    {
      if (Rml::Element* e = doc->GetElementById("formerror"))
      {
        e->SetInnerRML(textToRml(text));
      }
    }
  }
}

void MenuSDLRenderer::setNetOverlay(const NetOverlay& o)
{
  if (!m_overlay)
  {
    m_overlay = EventManager::getInstance()->LoadWindow("netoverlay.rml", false);
    if (!m_overlay)
    {
      return;
    }
    m_overlay->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
  }
  auto set = [this](const char* id, const std::string& rml) {
    if (Rml::Element* e = m_overlay->GetElementById(id))
    {
      if (e->GetInnerRML() != rml) e->SetInnerRML(rml);
    }
  };
  auto display = [this](const char* id, bool on) {
    if (Rml::Element* e = m_overlay->GetElementById(id))
    {
      e->SetProperty("display", on ? "block" : "none");
    }
  };
  set("stallbox", NetLobbyBinder::toRml(o.stall));
  display("stallbox", !o.stall.empty());
  std::string toasts;
  for (const std::string& t : o.toasts)
  {
    toasts += "<p>" + NetLobbyBinder::toRml(t) + "</p><br />";
  }
  set("toasts", toasts);
  set("desyncbox", NetLobbyBinder::toRml(o.desync));
  display("desyncbox", !o.desync.empty());
  set("joinbox", NetLobbyBinder::toRml(o.join));
  display("joinbox", !o.join.empty());
  set("netstatusline", NetLobbyBinder::toRml(o.status));
}

void MenuSDLRenderer::hideNetOverlay()
{
  if (m_overlay)
  {
    m_overlay->Close();
    m_overlay = nullptr;
  }
}

void MenuSDLRenderer::renderNetOverlay()
{
  if (!m_overlay)
  {
    return;
  }
  handleWindowResize();
  context->Update();
  context->Render();
}
#endif

void MenuSDLRenderer::setScoreboardPlayAgain(const std::string& label, bool enabled)
{
  Rml::ElementDocument* doc = findDocument(context, "scoreboard.rml");
  if (!doc)
  {
    return;
  }
  if (Rml::Element* e = doc->GetElementById("quickstart"))
  {
    e->SetInnerRML(textToRml(label.empty() ? std::string("Play Again") : label));
    e->SetProperty("color", enabled ? "#ffffff" : "#777777");
  }
}

void MenuSDLRenderer::handleWindowResize()
{
  int windowWidth, windowHeight;
  SDL_GetWindowSize(m_screen, &windowWidth, &windowHeight);
  if( context->GetDimensions().x != windowWidth || context->GetDimensions().y != windowHeight)
  {
    context->SetDimensions(Rml::Vector2i(windowWidth, windowHeight));
  }
}

bool MenuSDLRenderer::showMenu()
{
  SDL_Event event;

  bool continueRenderingMenu = true;

  if(! m_renderer)
  {
    MickLogger::getInstance()->error(this, "MenuRenderer::showMenu has nullptr for m_renderer!");
    return false;
  }

  handleWindowResize(); // Check for adjusted window size.
  
  context->Render();
  RmlUiSDL2SystemInterface* systemInterface = (RmlUiSDL2SystemInterface*) Rml::GetSystemInterface();

  // SDL delivers SDL_TEXTINPUT only while text input is on: on exactly when a text field has the focus, so the game's
  // own keys never go through an input method.
  const bool wantText = isTextField(context->GetFocusElement());
  if (wantText != m_textInputOn)
  {
    m_textInputOn = wantText;
    if (wantText) SDL_StartTextInput(); else SDL_StopTextInput();
  }

  while(SDL_PollEvent(&event))
  {
    // if the player select page is open, fire off it's specific SDLEventHandler
    if(context->GetFocusElement() && 
      EventHandlerPlayerSelect::RML_FILENAME == context->GetFocusElement()->GetOwnerDocument()->GetSourceURL())
    {
      continueRenderingMenu = m_eHandlerPlayerSelect->processSDLEvent(event);
    }
    else
    {
      switch(event.type)
      {
        case SDL_QUIT:
          continueRenderingMenu = false;
          MickLogger::getInstance()->debug(this, "SDL_QUIT");
          GameSettings::getInstance()->setGameState(GameSettings::GAME_QUIT);
          break;

        case SDL_MOUSEMOTION:
          context->ProcessMouseMove(event.motion.x, event.motion.y, systemInterface->GetKeyModifiers());
          break;
        case SDL_MOUSEBUTTONDOWN:
          context->ProcessMouseButtonDown(systemInterface->TranslateMouseButton(event.button.button), systemInterface->GetKeyModifiers());
          break;

        case SDL_MOUSEBUTTONUP:
          context->ProcessMouseButtonUp(systemInterface->TranslateMouseButton(event.button.button), systemInterface->GetKeyModifiers());
          break;

        case SDL_MOUSEWHEEL:
          context->ProcessMouseWheel(event.wheel.y, systemInterface->GetKeyModifiers());
          break;

        case SDL_TEXTINPUT:
          // Typing into a text field (the online host and join forms).
          context->ProcessTextInput(Rml::String(event.text.text));
          break;

        case SDL_KEYDOWN:
        {
            //printf("keydown: %d, sdl: %d\n", event.key, event.key.keysym.sym);
            // Intercept SHIFT + ~ key stroke to toggle libRmlUi's
            // visual debugger tool
            MickLogger::getInstance()->debug(this, std::string("SDL_KEYDOWN, sym:").append(std::to_string(event.key.keysym.sym)));
            if( event.key.keysym.sym == SDLK_BACKQUOTE &&
                event.key.keysym.mod == KMOD_LSHIFT )
            {
  #ifndef NDEBUG
              Rml::Debugger::SetVisible( ! Rml::Debugger::IsVisible() );
  #endif
              break;
            }
            else if(event.key.keysym.sym == SDLK_ESCAPE)
            {
              MickRmlUIElementUtil::menuBackButtonTriggered(context);
              break;
            }
            else if(event.key.keysym.sym == SDLK_DOWN
                    || event.key.keysym.sym == SDLK_UP
                    || (event.key.keysym.sym == SDLK_RETURN && isTextField(context->GetFocusElement())))
            {
              Rml::Element* focussedElement = context->GetFocusElement();
              if(!focussedElement)
              {
                break;
              }
              int currentTabIndex = MickRmlUIElementUtil::getTabIndex(focussedElement);
              // Enter in a text field goes on to the next one, as Down does. Pages that hide some of their
              // controls (the online lobby does, for a peer) have their indexes skipped over.
              Rml::Element* nextElement = MickRmlUIElementUtil::stepTabIndex(focussedElement->GetOwnerDocument(), currentTabIndex, (event.key.keysym.sym == SDLK_UP) ? -1 : 1);
              if(nextElement)
              {
                nextElement->Focus();
              }
              break;
            }

            context->ProcessKeyDown(systemInterface->TranslateKey(event.key.keysym.sym), systemInterface->GetKeyModifiers());
            break;
        }

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        {
          //event.jbutton.which, event.jbutton.button, event.jbutton.state
          if(event.jbutton.which == 0) // only process first controller in menu.
          {
            if(event.jbutton.state == SDL_PRESSED)
            {
              if(event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN
                  || event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP)
              {
                MickLogger::getInstance()->debug(this, "Got DPAD down/up.");
                Rml::Element* focussedElement = context->GetFocusElement();
                int currentTabIndex = MickRmlUIElementUtil::getTabIndex(focussedElement);
                Rml::Element* nextElement = MickRmlUIElementUtil::stepTabIndex(focussedElement->GetOwnerDocument(), currentTabIndex, (event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 1 : -1);
                if(nextElement)
                {
                  nextElement->Focus();
                }
              }
              else if (event.jbutton.button == SDL_CONTROLLER_BUTTON_A)
              {
                MickLogger::getInstance()->debug(this, "Got CTL A.");
                context->ProcessKeyDown(systemInterface->TranslateKey(SDLK_RETURN), systemInterface->GetKeyModifiers());
              }
              else if (event.jbutton.button == SDL_CONTROLLER_BUTTON_B)
              {
                MickLogger::getInstance()->debug(this, "Got CTL B.");
                MickRmlUIElementUtil::menuBackButtonTriggered(context);
              }
            }
          }
          break;
        }

        case SDL_CONTROLLERDEVICEADDED:
        {
          MickLogger::getInstance()->debug(this, string("Opening game controller: ").append(to_string(event.jbutton.which)));
          if (SDL_IsGameController(event.jbutton.which))
          {
            if(SDL_GameControllerOpen(event.jbutton.which))
            {
              MickLogger::getInstance()->debug(this, string("Opened game controller: ").append(to_string(event.jbutton.which)));
            }
          }
          break;
        }
        case SDL_CONTROLLERDEVICEREMOVED:
        {
          MickLogger::getInstance()->debug(this, string("Gamecontroller removed: ").append(to_string(event.jbutton.which)));
          break;
        }

        default:
          break;
      }
    }

  }
  context->Update();

  return continueRenderingMenu;
}

// WIP: continue on this to enable joining / assigning players to controllers/keyboard.
/*
int MenuSDLRenderer::processEventForJoystick(SDL_JoystickID controllerId, SDL_JoyButtonEvent buttonEvent)
{
  if(joystickToFocussedElement.find(controllerId) != joystickToFocussedElement.end())
  {
    Rml::Element *focussedElementForJoystick = joystickToFocussedElement.find(controllerId)->second;
    if(focussedElementForJoystick->GetOwnerDocument() == context->GetFocusElement()->GetOwnerDocument())
    {
      // we're on the right menu page, try to process the event
      int currentTabIndex = MickRmlUIElementUtil::getTabIndex(focussedElementForJoystick);
      Rml::Element* nextElement = MickRmlUIElementUtil::getElementWithTabIndex(focussedElementForJoystick->GetOwnerDocument(), (buttonEvent.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? ++currentTabIndex : --currentTabIndex);
      if(nextElement)
      {
        nextElement->Focus();
      }
    }
    else
    {
      // if we're on the PlayerSelect page, and this is button 'A', treat this as a "join" event, otherwise ignore.
    }
    
  }
}
*/

/**
 * 
 */


MenuSDLRenderer::~MenuSDLRenderer()
{
  Rml::Shutdown();
  if(m_scoreBinder)
  {
    delete m_scoreBinder;
    m_scoreBinder = nullptr;
  }
}

