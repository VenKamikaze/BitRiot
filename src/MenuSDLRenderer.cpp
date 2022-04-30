// implementation of Map.h

#include "MenuSDLRenderer.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/binders/PlayersBinder.h"
#include "RmlUI/events/EventHandlerPlayerSelect.h"
#include "SDL_gamecontroller.h"
#include "SDL_keycode.h"
#include <RmlUi/Core/ElementDocument.h>


using namespace std;

MenuSDLRenderer::MenuSDLRenderer(SDL_Renderer* renderer, SDL_Window* screen)
{
  init(renderer, screen);
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

void MenuSDLRenderer::init(SDL_Renderer* renderer, SDL_Window *screen)
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

  m_context = Rml::CreateContext("default",
                                   Rml::Vector2i(windowWidth, windowHeight));

#ifndef NDEBUG
  Rml::Debugger::Initialise(m_context);
#endif

  // Initialise the event instancer and handlers.
  EventInstancer* event_instancer = new EventInstancer();
  Rml::Factory::RegisterEventListenerInstancer(event_instancer);

  EventManager::getInstance()->RegisterEventHandler("gameoptions.rml", new EventHandlerOptions());
  EventManager::getInstance()->RegisterEventHandler("playerselect.rml", new EventHandlerPlayerSelect());
  
  m_playersBinder = make_unique<PlayersBinder>(m_context);

  loadMenu("mainmenu.rml");
  m_renderer = renderer;
  m_screen = screen;
}

void MenuSDLRenderer::loadScoreBoard(shared_ptr<PlayerCharacterEntity> winner, int gameTotalLength)
{
  m_scoreBinder = new ScoreBoardBinder(m_context, winner, gameTotalLength);
  Rml::ElementDocument *doc = loadMenu("scoreboard.rml", (winner != nullptr));
  if(! winner)
  {
    MickLogger::getInstance()->debug(this, "No winner - hiding winner stats");
    doc->GetElementById("winnerStats")->SetClass("invisible", true);
    doc->Focus();
    doc->Show();
  }
}

void MenuSDLRenderer::clearScoreBoard()
{
  if(m_scoreBinder && m_context)
  {
    m_scoreBinder->RemoveDataBinding(m_context);
    delete m_scoreBinder;
    m_scoreBinder = nullptr;
  }
}

void MenuSDLRenderer::handleWindowResize()
{
  int windowWidth, windowHeight;
  SDL_GetWindowSize(m_screen, &windowWidth, &windowHeight);
  if( m_context->GetDimensions().x != windowWidth || m_context->GetDimensions().y != windowHeight)
  {
    m_context->SetDimensions(Rml::Vector2i(windowWidth, windowHeight));
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
  
  m_context->Render();
  RmlUiSDL2SystemInterface* systemInterface = (RmlUiSDL2SystemInterface*) Rml::GetSystemInterface();

  while(SDL_PollEvent(&event))
  {
    switch(event.type)
    {
      case SDL_QUIT:
        continueRenderingMenu = false;
        MickLogger::getInstance()->debug(this, "SDL_QUIT");
        GameSettings::getInstance()->setGameState(GameSettings::GAME_QUIT);
        break;

      case SDL_MOUSEMOTION:
        m_context->ProcessMouseMove(event.motion.x, event.motion.y, systemInterface->GetKeyModifiers());
        break;
      case SDL_MOUSEBUTTONDOWN:
        m_context->ProcessMouseButtonDown(systemInterface->TranslateMouseButton(event.button.button), systemInterface->GetKeyModifiers());
        break;

      case SDL_MOUSEBUTTONUP:
        m_context->ProcessMouseButtonUp(systemInterface->TranslateMouseButton(event.button.button), systemInterface->GetKeyModifiers());
        break;

      case SDL_MOUSEWHEEL:
        m_context->ProcessMouseWheel(event.wheel.y, systemInterface->GetKeyModifiers());
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
            menuBackButtonHit();
            break;
          }
          else if(event.key.keysym.sym == SDLK_DOWN
                  || event.key.keysym.sym == SDLK_UP)
          {
            Rml::Element* focussedElement = m_context->GetFocusElement();
            int currentTabIndex = MickRmlUIElementUtil::getTabIndex(focussedElement);
            Rml::Element* nextElement = MickRmlUIElementUtil::getElementWithTabIndex(focussedElement->GetOwnerDocument(), (event.key.keysym.sym == SDLK_DOWN) ? ++currentTabIndex : --currentTabIndex);
            if(nextElement)
            {
              nextElement->Focus();
            }
            break;
          }

          m_context->ProcessKeyDown(systemInterface->TranslateKey(event.key.keysym.sym), systemInterface->GetKeyModifiers());
          break;
      }

        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
        {
          Rml::Element* focussedElement = m_context->GetFocusElement();
          int currentTabIndex = MickRmlUIElementUtil::getTabIndex(focussedElement);
          Rml::Element* nextElement = MickRmlUIElementUtil::getChildElementWithTabIndex(focussedElement->GetParentNode(), (event.key.keysym.sym == SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? ++currentTabIndex : --currentTabIndex);
          if(nextElement)
          {
            nextElement->Focus();
          }
          break;
        }

        case SDL_CONTROLLER_BUTTON_A:
        {
          m_context->ProcessKeyDown(systemInterface->TranslateKey(SDLK_RETURN), systemInterface->GetKeyModifiers());
          break;
        }

        case SDL_CONTROLLER_BUTTON_B:
        {
          menuBackButtonHit();
          break;
        }

      default:
        break;
    }
  }
  m_context->Update();

  return continueRenderingMenu;
}

void MenuSDLRenderer::menuBackButtonHit()
{
  RmlUiSDL2SystemInterface* systemInterface = (RmlUiSDL2SystemInterface*) Rml::GetSystemInterface();
  Rml::ElementDocument *document = m_context->GetFocusElement()->GetOwnerDocument();
  if(document)
  {
    MickLogger::getInstance()->debug(this, std::string("current document id: ").append(document ? document->GetId() : "null") );
    Rml::Element* escapeActionElement = MickRmlUIElementUtil::getFirstElementWithAttribute(document, "escapeAction");
    if(escapeActionElement)
    {
      MickLogger::getInstance()->debug(this, std::string("escapeActionElement element id: ").append(escapeActionElement->GetId()) );
      // process the element with the attribute 'escapeAction'
      escapeActionElement->Focus();
      m_context->ProcessKeyDown(systemInterface->TranslateKey(SDLK_RETURN), 0);
    }
  }
}

MenuSDLRenderer::~MenuSDLRenderer()
{
  Rml::Shutdown();
  if(m_scoreBinder)
  {
    delete m_scoreBinder;
    m_scoreBinder = nullptr;
  }
}

