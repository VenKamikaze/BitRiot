// implementation of Map.h

#include "MenuSDLRenderer.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/events/EventHandlerPlayerSelect.h"
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
  context->UnloadAllDocuments();
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
                    || event.key.keysym.sym == SDLK_UP)
            {
              Rml::Element* focussedElement = context->GetFocusElement();
              int currentTabIndex = MickRmlUIElementUtil::getTabIndex(focussedElement);
              Rml::Element* nextElement = MickRmlUIElementUtil::getElementWithTabIndex(focussedElement->GetOwnerDocument(), (event.key.keysym.sym == SDLK_DOWN) ? ++currentTabIndex : --currentTabIndex);
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
                Rml::Element* nextElement = MickRmlUIElementUtil::getElementWithTabIndex(focussedElement->GetOwnerDocument(), (event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? ++currentTabIndex : --currentTabIndex);
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

