// implementation of Map.h

#include "MenuRenderer.h"


using namespace std;

MenuRenderer::MenuRenderer(SDL_Renderer* renderer, SDL_Window* screen)
{
	init(renderer, screen);
}

void MenuRenderer::init(SDL_Renderer* renderer, SDL_Window *screen)
{
	GLenum err = glewInit();

	if(err != GLEW_OK)
	{
		fprintf(stderr, "GLEW ERROR: %s\n", glewGetErrorString(err));
		throw std::runtime_error("GLEW ERROR");
	}

	int window_width, window_height;
	SDL_GetWindowSize(screen, &window_width, &window_height);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    glMatrixMode(GL_PROJECTION|GL_MODELVIEW);
    glLoadIdentity();
    glOrtho(0, window_width, window_height, 0, 0, 1);

	RocketSDL2Renderer* rocketRenderGlue = new RocketSDL2Renderer(renderer, screen);
	RocketSDL2SystemInterface* rocketSystemGlue = new RocketSDL2SystemInterface();
	ShellFileInterface* rocketFileGlue = new ShellFileInterface("../Samples/assets/");

	Rocket::Core::SetFileInterface(rocketFileGlue);
	Rocket::Core::SetRenderInterface(rocketRenderGlue);
    Rocket::Core::SetSystemInterface(rocketSystemGlue);

	if(!Rocket::Core::Initialise())
		throw std::runtime_error(std::string("Unable to init libRocket."));

	Rocket::Core::FontDatabase::LoadFontFace("Delicious-Bold.otf");
	Rocket::Core::FontDatabase::LoadFontFace("Delicious-BoldItalic.otf");
	Rocket::Core::FontDatabase::LoadFontFace("Delicious-Italic.otf");
	Rocket::Core::FontDatabase::LoadFontFace("Delicious-Roman.otf");

	Rocket::Core::Context *Context = Rocket::Core::CreateContext("default",
		Rocket::Core::Vector2i(window_width, window_height));

	Rocket::Debugger::Initialise(Context);

	Rocket::Core::ElementDocument *Document = Context->LoadDocument("demo.rml");

	if(Document)
	{
		Document->Show();
		Document->RemoveReference();
		m_context = Context;
	}
	else
	{
		fprintf(stdout, "\nDocument is NULL");
		throw std::runtime_error(std::string("Menu RML was not found"));
	}
}

bool MenuRenderer::showMenu()
{
    SDL_Event event;

    bool menuDone = false;

	SDL_Renderer* renderer = ((RocketSDL2Renderer*) m_context->GetRenderInterface())->GetSDLRenderer();

	SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_RenderClear(renderer);
	m_context->Render();
    SDL_RenderPresent(renderer);

    RocketSDL2SystemInterface* systemInterface = (RocketSDL2SystemInterface*) Rocket::Core::GetSystemInterface();

    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_QUIT:
            	menuDone = true;
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
                // Intercept SHIFT + ~ key stroke to toggle libRocket's
                // visual debugger tool
                if( event.key.keysym.sym == SDLK_BACKQUOTE &&
                    event.key.keysym.mod == KMOD_LSHIFT )
                {
                    Rocket::Debugger::SetVisible( ! Rocket::Debugger::IsVisible() );
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

    return menuDone;
}

MenuRenderer::~MenuRenderer()
{
	if(m_context)
	{
	    m_context->RemoveReference();
	    m_context = NULL;
	}
    Rocket::Core::Shutdown();
}

