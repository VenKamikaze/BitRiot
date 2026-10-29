// WebHooks.h - the web build's glue between the game and its page (brief 09).
//
// Emscripten only; the file is not compiled for any other target. The page
// (web/shell.html) and the Playwright smoke test (tests/web/) talk to the game
// through the functions exported at the bottom of WebHooks.cpp:
//
//   Module._bitriot_frames()           main-loop iterations run so far
//   Module._bitriot_tick()             the simulation's tick count (see GameEngine::simTick)
//   Module._bitriot_game_state()       GameSettings::GAME_STATE as an int
//   Module._bitriot_start_ai_match(n)  start a round of n CPU players; 1 if it started
//   Module._bitriot_sounds_played()    sounds the mixer has accepted (audio reached SDL_mixer)
//   Module._bitriot_joystick_count()   SDL_NumJoysticks()
//
// and through Module.bitriotFatal(message) / Module.bitriotQuit() callbacks that the
// page may define. The page may also be opened with ?autostart=ai4 (any count from
// 2 to 6), which starts a CPU-only round on the first frame.
#ifndef WEBHOOKS_H_
#define WEBHOOKS_H_

#ifdef __EMSCRIPTEN__

#include <string>

class GameEngine;

// Called once, after the engine and menus exist and before the main loop starts.
void webHooksInit(GameEngine* engine);
// Called once per main-loop iteration, after the frame.
void webHooksAfterFrame();
// Shows `message` on the page (Module.bitriotFatal) and logs it; the game is stopping.
void webReportFatal(const std::string& message);
// Tells the page the game has quit and shut down (Module.bitriotQuit).
void webReportQuit();

#endif /* __EMSCRIPTEN__ */

#endif /* WEBHOOKS_H_ */
