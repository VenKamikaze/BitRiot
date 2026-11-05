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
//   Module._bitriot_net_start(host, name, room, localPlayers)   (brief 12) start a relay game: open a room, or join `room`
//   Module._bitriot_net_room() / _net_transport() / _net_state() / _net_press_start() / _net_log() / _net_info()
//   Module._bitriot_hidden_polls() / _is_hidden()
//
// All of those answer 0 (or an empty string) unless the page was opened with ?test=1 (or sets Module.bitriotTest): a public page
// offers no way to start games or read the simulation from outside.
//
// and through Module.bitriotFatal(message) / Module.bitriotQuit() callbacks that the
// page may define. The page may also be opened with ?autostart=ai4 (any count from
// 2 to 6; five or six raise the map height so all portraits fit), which starts a
// CPU-only round on the first frame.
//
// After the game has quit (webReportQuit) every hook answers 0, except
// _bitriot_frames, which keeps its last count.
//
// (Brief 09 shipped the hooks open and asked for them to be gated before a public page;
// brief 12 gated them behind ?test=1.)
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
// Runs one pass of the main loop for a tab that is hidden (it gets no animation frames): from the page's message and timer
// events (see WebHooks.cpp). Defined in main.cpp. Does nothing when the game is not running or the tab is not hidden.
void webHiddenPoll();
// Counted by webHiddenPoll() for the test hook bitriot_hidden_polls().
void webCountHiddenPoll();
// Puts `text` on the browser's clipboard (the lobby's "Copy room code").
void webCopyToClipboard(const std::string& text);

#endif /* __EMSCRIPTEN__ */

#endif /* WEBHOOKS_H_ */
