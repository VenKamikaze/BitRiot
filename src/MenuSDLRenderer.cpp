// implementation of Map.h

#include "MenuSDLRenderer.h"
#include "MickLogger.h"
#include "PlayerCharacterEntity.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/ScoreBoardBinder.h"
#include "SDL_keycode.h"
#include "SDL_pixels.h"
#include "SDL_render.h"
#include <string>


using namespace std;

MenuSDLRenderer::MenuSDLRenderer(SDL_Renderer* renderer, SDL_Window* screen)
{
  init(renderer, screen);
}

void MenuSDLRenderer::loadMenu(std::string menuRmlFile)
{
  if (! EventManager::getInstance()->LoadWindow(menuRmlFile.c_str()))
  {
    fprintf(stdout, "\nDocument is NULL");
    throw std::runtime_error(std::string("Menu RML was not found"));
  }
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

  Rml::Debugger::Initialise(m_context);

  // Initialise the event instancer and handlers.
  EventInstancer* event_instancer = new EventInstancer();
  Rml::Factory::RegisterEventListenerInstancer(event_instancer);

  EventManager::getInstance()->RegisterEventHandler("gameoptions.rml", new EventHandlerOptions());
  loadMenu("mainmenu.rml");
  m_renderer = renderer;
  m_screen = screen;
}

void MenuSDLRenderer::loadScoreBoard(PlayerCharacterEntity *winner, int gameTotalLength)
{
  m_scoreBinder = new ScoreBoardBinder(m_context, winner, gameTotalLength);
  loadMenu("scoreboard.rml");
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

bool MenuSDLRenderer::showMenu()
{
  SDL_Event event;

  bool continueRenderingMenu = true;

  if(! m_renderer)
  {
    MickLogger::getInstance()->error(this, "MenuRenderer::showMenu has nullptr for m_renderer!");
    return false;
  }
  
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
            Rml::Debugger::SetVisible( ! Rml::Debugger::IsVisible() );
            break;
          }
          else if(event.key.keysym.sym == SDLK_ESCAPE)
          {
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
            break;
          }
          else if(event.key.keysym.sym == SDLK_DOWN
                  || event.key.keysym.sym == SDLK_UP)
          {
            Rml::Element* focussedElement = m_context->GetFocusElement();
            int currentTabIndex = MickRmlUIElementUtil::getTabIndex(focussedElement);
            Rml::Element* nextElement = MickRmlUIElementUtil::getChildElementWithTabIndex(focussedElement->GetParentNode(), (event.key.keysym.sym == SDLK_DOWN) ? ++currentTabIndex : --currentTabIndex);
            if(nextElement)
            {
              nextElement->Focus();
            }
            break;
          }

          m_context->ProcessKeyDown(systemInterface->TranslateKey(event.key.keysym.sym), systemInterface->GetKeyModifiers());
          break;
        }

      default:
        break;
    }
  }
  m_context->Update();

  return continueRenderingMenu;
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

