// implementation of Map.h

#include "MenuRenderer.h"
#include <iostream>


using namespace std;

MenuRenderer::MenuRenderer(SDL_Renderer* renderer, SDL_Window* screen)
{
  init(renderer, screen);
}

void MenuRenderer::loadMenu(std::string menuRmlFile)
{
  if (! EventManager::getInstance()->LoadWindow(menuRmlFile.c_str()))
  {
    fprintf(stdout, "\nDocument is NULL");
    throw std::runtime_error(std::string("Menu RML was not found"));
  }
}

void MenuRenderer::init(SDL_Renderer* renderer, SDL_Window *screen)
{
  int window_width, window_height;
  SDL_GetWindowSize(screen, &window_width, &window_height);
  RmlUiSDL2Renderer* rmluiRenderGlue = new RmlUiSDL2Renderer(renderer, screen);
  RmlUiSDL2SystemInterface* rmluiSystemGlue = new RmlUiSDL2SystemInterface();
  ShellFileInterface* rmluiFileGlue = new ShellFileInterface("assets/menu/");

  Rml::SetFileInterface(rmluiFileGlue);
  Rml::SetRenderInterface(rmluiRenderGlue);
  Rml::SetSystemInterface(rmluiSystemGlue);

  if(!Rml::Initialise())
  {
    throw std::runtime_error(std::string("Unable to init libRmlUi."));
  }

  Rml::LoadFontFace("assets/fonts/FreeMono.ttf");
  // These appear to be non-free
  //Rml::LoadFontFace("assets/fonts/Delicious-Bold.otf");
  //Rml::LoadFontFace("assets/fonts/Delicious-BoldItalic.otf");
  //Rml::LoadFontFace("assets/fonts/Delicious-Italic.otf");
  //Rml::LoadFontFace("assets/fonts/Delicious-Roman.otf");

  m_context = Rml::CreateContext("default",
                                   Rml::Vector2i(window_width, window_height));

  Rml::Debugger::Initialise(m_context);

  // Initialise the event instancer and handlers.
  EventInstancer* event_instancer = new EventInstancer();
  Rml::Factory::RegisterEventListenerInstancer(event_instancer);

  EventManager::getInstance()->RegisterEventHandler("gameoptions.rml", new EventHandlerOptions());
  loadMenu("mainmenu.rml");
  m_renderer = renderer;
}

bool MenuRenderer::showMenu()
{
  SDL_Event event;

  bool continueRenderingMenu = true;

  if(m_renderer == nullptr)
  {
    cerr << "MenuRenderer::showMenu has nullptr for m_renderer!" << endl;
    return false;
  }

  SDL_SetRenderDrawColor(m_renderer, 0, 0, 255, 255);
  SDL_RenderClear(m_renderer);
  m_context->Render();
  SDL_RenderPresent(m_renderer);

  RmlUiSDL2SystemInterface* systemInterface = (RmlUiSDL2SystemInterface*) Rml::GetSystemInterface();

  while(SDL_PollEvent(&event))
  {
    switch(event.type)
    {
      case SDL_QUIT:
        continueRenderingMenu = false;
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
          if( event.key.keysym.sym == SDLK_BACKQUOTE &&
              event.key.keysym.mod == KMOD_LSHIFT )
          {
            Rml::Debugger::SetVisible( ! Rml::Debugger::IsVisible() );
            break;
          }
          else if(event.key.keysym.sym == SDLK_ESCAPE)
          {
            continueRenderingMenu = false;
            GameSettings::getInstance()->setGameState(GameSettings::GAME_OVER);
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

MenuRenderer::~MenuRenderer()
{
  Rml::Shutdown();
  if(m_context)
  {
    delete m_context;
    m_context = nullptr;
  }
}

