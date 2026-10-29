// WebHooks.cpp - see WebHooks.h.

#ifdef __EMSCRIPTEN__

#include "web/WebHooks.h"

#include <emscripten.h>
#include <SDL2/SDL.h>

#include "GameEngine.h"
#include "GameSettings.h"
#include "MickLogger.h"
#include "MickSDLSound.h"

namespace
{
GameEngine* s_engine = nullptr;
int s_frames = 0;
int s_autostartPlayers = 0;   // from ?autostart=aiN, 0 when absent
bool s_autostartDone = false;
}

// The N of ?autostart=aiN in the page's address, or 0.
EM_JS(int, bitriot_js_autostart_players, (), {
  const match = /[?&]autostart=ai([0-9]+)/.exec(window.location.search);
  return match ? parseInt(match[1], 10) : 0;
});

EM_JS(void, bitriot_js_fatal, (const char* message), {
  const text = UTF8ToString(message);
  if (typeof Module.bitriotFatal === 'function') Module.bitriotFatal(text);
});

EM_JS(void, bitriot_js_quit, (), {
  if (typeof Module.bitriotQuit === 'function') Module.bitriotQuit();
});

void webHooksInit(GameEngine* engine)
{
  s_engine = engine;
  s_autostartPlayers = bitriot_js_autostart_players();
}

void webHooksAfterFrame()
{
  s_frames++;
  if (s_autostartPlayers > 0 && !s_autostartDone && s_engine)
  {
    // The first frame has drawn the main menu, so the engine is in MENU_RUNNING.
    s_autostartDone = true;
    if (!s_engine->startAllAiMatch(s_autostartPlayers))
    {
      std::MickLogger::getInstance()->warn(nullptr, "autostart: could not start a CPU-only round");
    }
  }
}

void webReportFatal(const std::string& message)
{
  std::MickLogger::getInstance()->error(nullptr, message.c_str());
  bitriot_js_fatal(message.c_str());
}

void webReportQuit()
{
  bitriot_js_quit();
}

extern "C"
{

EMSCRIPTEN_KEEPALIVE int bitriot_frames()
{
  return s_frames;
}

EMSCRIPTEN_KEEPALIVE int bitriot_tick()
{
  return s_engine ? (int) s_engine->simTick() : 0;
}

EMSCRIPTEN_KEEPALIVE int bitriot_game_state()
{
  return (int) std::GameSettings::getInstance()->getGameState();
}

EMSCRIPTEN_KEEPALIVE int bitriot_start_ai_match(int numPlayers)
{
  return (s_engine && s_engine->startAllAiMatch(numPlayers)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bitriot_sounds_played()
{
  return (int) MickSDLSound::getInstance()->soundsPlayed();
}

EMSCRIPTEN_KEEPALIVE int bitriot_joystick_count()
{
  return SDL_NumJoysticks();
}

} // extern "C"

#endif /* __EMSCRIPTEN__ */
