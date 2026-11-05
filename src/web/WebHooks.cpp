// WebHooks.cpp - see WebHooks.h.

#ifdef __EMSCRIPTEN__

#include "web/WebHooks.h"

#include <emscripten.h>
#include <SDL2/SDL.h>

#include "GameEngine.h"
#include "GameSettings.h"
#include "MickLogger.h"
#include "MatchReplay.h"
#include "MickSDLSound.h"
#ifdef BITRIOT_NETPLAY
#include "NetGame.h"
#include "RmlUI/events/EventHandlerNetLobby.h"
#include "net/LobbyModel.h"
#endif

namespace
{
GameEngine* s_engine = nullptr;
int s_frames = 0;
int s_autostartPlayers = 0;   // from ?autostart=aiN, 0 when absent
bool s_autostartDone = false;
bool s_quit = false;          // the game has shut down; the hooks answer 0
bool s_devKnobs = false;      // a dev knob was honoured (only possible with ?test=1)
bool s_testMode = false;      // the page was opened with ?test=1: the test hooks answer
bool s_hidden = false;        // document.hidden
int s_hiddenPolls = 0;        // frames the page ran for a hidden tab
std::string s_lastRoom;       // the room code the page was last told
std::string s_returned;       // storage for the strings the exported hooks return
}

// -- the page, from the game ---------------------------------------------------------------------

// ?test=1 in the address (or Module.bitriotTest): the page is a test or development page. The hooks that start games and read the
// simulation from outside answer only then, so a public page does not offer them.
EM_JS(int, bitriot_js_test_mode, (), {
  return (/[?&]test=1(&|$)/.test(window.location.search) || Module.bitriotTest) ? 1 : 0;
});

// A query parameter of the page's address, or NULL (malloc'd; the caller frees it).
EM_JS(char*, bitriot_js_query, (const char* key), {
  const value = new URLSearchParams(window.location.search).get(UTF8ToString(key));
  if (value === null) return 0;
  const bytes = lengthBytesUTF8(value) + 1;
  const pointer = _malloc(bytes);
  stringToUTF8(value, pointer, bytes);
  return pointer;
});

// The relay a visitor is offered: ?relay=, else window.BITRIOT_RELAY (a hosting page sets it before the game script), else the
// server the page came from (wss://host/vnet for https, ws://host:8080/vnet, the relay's own port, for http).
EM_JS(char*, bitriot_js_default_relay, (), {
  let value = new URLSearchParams(window.location.search).get('relay') || window.BITRIOT_RELAY || "";
  if (!value) {
    const host = window.location.hostname.indexOf(':') >= 0 ? '[' + window.location.hostname + ']' : window.location.hostname;
    value = window.location.protocol === 'https:' ? 'wss://' + window.location.host + '/vnet' : 'ws://' + host + ':8080/vnet';
  }
  const bytes = lengthBytesUTF8(value) + 1;
  const pointer = _malloc(bytes);
  stringToUTF8(value, pointer, bytes);
  return pointer;
});

// A string a test left for the game in Module.bitriotTestArgs (strings cannot be passed to an exported function without the
// runtime's allocator, which this build does not export). NULL (malloc'd otherwise; the caller frees it).
EM_JS(char*, bitriot_js_test_arg, (const char* key), {
  const args = Module.bitriotTestArgs || {};
  const value = args[UTF8ToString(key)];
  if (value === undefined || value === null) return 0;
  const text = String(value);
  const bytes = lengthBytesUTF8(text) + 1;
  const pointer = _malloc(bytes);
  stringToUTF8(text, pointer, bytes);
  return pointer;
});

EM_JS(int, bitriot_js_page_is_secure, (), {
  return window.location.protocol === 'https:' ? 1 : 0;
});

// The saved settings live in the browser's localStorage; the game reads and writes them as a file (brief 08's netplay.txt)
// in its in-memory file system. These copy the text across, around each read and write of that file.
EM_JS(void, bitriot_js_prefs_load, (const char* path), {
  try {
    const text = window.localStorage.getItem('bitriot.netplay');
    if (text !== null) {
      const file = UTF8ToString(path);
      FS.writeFile(file, text);
    }
  } catch (e) { /* storage blocked (a private window): the form opens with its defaults */ }
});

EM_JS(void, bitriot_js_prefs_store, (const char* path), {
  try {
    window.localStorage.setItem('bitriot.netplay', FS.readFile(UTF8ToString(path), { encoding: 'utf8' }));
  } catch (e) { /* storage blocked or full: the settings last as long as the page */ }
});

EM_JS(void, bitriot_js_copy, (const char* text), {
  const value = UTF8ToString(text);
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(value).catch(function () {});
  }
});

// Tells the page which room the game is in (Module.bitriotRoom(code, relay); "" when none) and the relay it is on, for the panel under the canvas.
EM_JS(void, bitriot_js_room, (const char* code, const char* relay), {
  if (typeof Module.bitriotRoom === 'function') Module.bitriotRoom(UTF8ToString(code), UTF8ToString(relay));
});

// A hidden tab is driven by network events, not by animation frames (brief 12, doco/netplay/12-browser-netplay.md). The
// page tells the game when it is hidden; vnet's wake callback (called from the browser event that queued a datagram) posts a
// message to ourselves, which runs a frame; and a timer does the same once a second, for vnet's own deadlines. A message
// event is not throttled in a background tab, a timer is (to about once a second, and less after five minutes).
EM_JS(void, bitriot_js_install_hidden_driver, (int wakeToo), {
  const report = function () { Module._bitriot_set_hidden(document.hidden ? 1 : 0); };
  document.addEventListener('visibilitychange', report);
  report();
  const channel = new MessageChannel();
  let pending = false;
  channel.port1.onmessage = function () {
    pending = false;
    if (document.hidden) Module._bitriot_hidden_poll();
  };
  Module.bitriotWake = function () {
    if (wakeToo && !pending && document.hidden) {
      pending = true;
      channel.port2.postMessage(0);
    }
  };
  setInterval(function () { if (document.hidden) Module._bitriot_hidden_poll(); }, 1000);
});

EM_JS(void, bitriot_js_wake, (), {
  if (Module.bitriotWake) Module.bitriotWake();
});

static std::string queryOf(const char* key)
{
  char* raw = bitriot_js_query(key);
  if (raw == nullptr)
  {
    return std::string();
  }
  std::string value = raw;
  free(raw);
  return value;
}

// Text for inside a JSON string.
static std::string jsonText(const std::string& text)
{
  std::string out;
  for (char c : text) out += (c == '"' || c == '\\' || (unsigned char) c < 0x20) ? ' ' : c;
  return out;
}

static std::string takeString(char* raw)
{
  std::string value = raw != nullptr ? raw : "";
  free(raw);
  return value;
}

#ifdef BITRIOT_NETPLAY
// vnet_config.wake: called from a browser event, never from inside a vnet call. It only arranges a poll.
static void webWake()
{
  bitriot_js_wake();
}
#endif

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
  s_testMode = bitriot_js_test_mode() != 0;
  s_autostartPlayers = s_testMode ? bitriot_js_autostart_players() : 0;
#ifdef BITRIOT_NETPLAY
  const bool secure = bitriot_js_page_is_secure() != 0;
  EventHandlerNetForm::setRelayDefaults(takeString(bitriot_js_default_relay()), queryOf("room"), secure, !queryOf("relay").empty());
  EventHandlerNetForm::setPrefsSyncHooks([](const std::string& path) { bitriot_js_prefs_load(path.c_str()); },
                                         [](const std::string& path) { bitriot_js_prefs_store(path.c_str()); });
  NetGame::setWakeHandler(&webWake);
  // Development and test knobs for the relay connection: an explicit WebTransport URL, the hash of a development certificate,
  // and a vnet impairment (the browser has no environment variables).
  NetGame::Params dev;
  // Only on a ?test=1 page: a link someone else wrote must not route a visitor through another WebTransport relay while the form shows
  // the real one, or degrade the visitor's connection. (?relay= and ?room= stay open: the relay is on the form for all to see.)
  if (s_testMode)
  {
    dev.relayWt = queryOf("relay_wt");
    dev.relayCertSha256 = queryOf("relay_cert");
    dev.impair = queryOf("impair");
    s_devKnobs = !dev.relayWt.empty() || !dev.relayCertSha256.empty() || !dev.impair.empty();
  }
  engine->setNetDevParams(dev);
  // ?hiddendriver=off|timer (test knobs): off leaves a hidden tab to the browser (nothing runs), timer drives it from the one-second
  // timer only, no network wake-ups: what tests/web/netplay.js "background" compares the default (wake-ups and the timer) with.
  const std::string driver = s_testMode ? queryOf("hiddendriver") : std::string();
  if (!driver.empty()) s_devKnobs = true;
  if (driver == "off")
  {
    EM_ASM({ document.addEventListener('visibilitychange', function () { Module._bitriot_set_hidden(document.hidden ? 1 : 0); }); });
  }
  else
  {
    bitriot_js_install_hidden_driver(driver == "timer" ? 0 : 1);
  }
#endif
}

void webCopyToClipboard(const std::string& text)
{
  bitriot_js_copy(text.c_str());
}

void webCountHiddenPoll()
{
  s_hiddenPolls++;
}

void webHooksAfterFrame()
{
  s_frames++;
#ifdef BITRIOT_NETPLAY
  if (s_engine)
  {
    const std::string room = s_engine->netRoomCode() + "|" + s_engine->netRelayText();
    if (room != s_lastRoom)
    {
      s_lastRoom = room;
      bitriot_js_room(s_engine->netRoomCode().c_str(), s_engine->netRelayText().c_str());
    }
  }
#endif
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
  // The engine and the sound device are gone: the exported hooks must not touch them.
  s_engine = nullptr;
  s_quit = true;
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
  return (s_testMode && !s_quit && s_engine) ? (int) s_engine->simTick() : 0;
}

EMSCRIPTEN_KEEPALIVE int bitriot_game_state()
{
  return (s_testMode && !s_quit) ? (int) std::GameSettings::getInstance()->getGameState() : 0;
}

EMSCRIPTEN_KEEPALIVE int bitriot_start_ai_match(int numPlayers)
{
  return (s_testMode && s_engine && s_engine->startAllAiMatch(numPlayers)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bitriot_sounds_played()
{
  return (s_testMode && !s_quit) ? (int) MickSDLSound::getInstance()->soundsPlayed() : 0;
}

EMSCRIPTEN_KEEPALIVE int bitriot_joystick_count()
{
  return (s_testMode && !s_quit) ? SDL_NumJoysticks() : 0;
}

// -- the page's visibility (always on: the game must know it is hidden) ---------------------------

EMSCRIPTEN_KEEPALIVE void bitriot_set_hidden(int hidden)
{
  s_hidden = hidden != 0;
  if (s_engine)
  {
    s_engine->setHidden(s_hidden);
  }
}

EMSCRIPTEN_KEEPALIVE void bitriot_hidden_poll()
{
  webHiddenPoll();
}

#ifdef BITRIOT_NETPLAY
// -- network test hooks (?test=1 only). They start relay games and read what the machines played, so that a
// Playwright test can drive browsers without clicking through the menus, and compare their rounds with a native player's. ----

// Starts a relay game: host 1 opens a room, host 0 joins Module.bitriotTestArgs.room. The player's name is
// Module.bitriotTestArgs.name. The relay is the page's (?relay=, as the forms offer it).
EMSCRIPTEN_KEEPALIVE int bitriot_net_start(int host, int localPlayers)
{
  if (!s_testMode || s_quit || !s_engine)
  {
    return 0;
  }
  const std::string relay = takeString(bitriot_js_default_relay());
  const std::string name = takeString(bitriot_js_test_arg("name"));
  const std::string room = takeString(bitriot_js_test_arg("room"));
  std::string error;
  const bool ok = s_engine->startRelayFromText(host != 0, name, relay, room, localPlayers, bitriot_js_page_is_secure() != 0, &error);
  if (!ok)
  {
    std::string message = "bitriot_net_start: " + error;
    std::MickLogger::getInstance()->warn(nullptr, message);
  }
  return ok ? 1 : 0;
}

// The room code ("" until the relay has said it).
EMSCRIPTEN_KEEPALIVE const char* bitriot_net_room()
{
  s_returned = (s_testMode && !s_quit && s_engine) ? s_engine->netRoomCode() : std::string();
  return s_returned.c_str();
}

// What carries the datagrams: "relay-wt", "relay-ws", or "".
EMSCRIPTEN_KEEPALIVE const char* bitriot_net_transport()
{
  NetGame* net = (s_testMode && !s_quit && s_engine) ? s_engine->netGame() : nullptr;
  s_returned = net != nullptr ? net->transport() : std::string();
  return s_returned.c_str();
}

// 0 no network game, 1 connecting, 2 in the lobby, 3 a round is running, 4 between rounds (the scoreboard), 5 ended.
EMSCRIPTEN_KEEPALIVE int bitriot_net_state()
{
  NetGame* net = (s_testMode && !s_quit && s_engine) ? s_engine->netGame() : nullptr;
  if (net == nullptr)
  {
    return 0;
  }
  switch (net->session().state())
  {
    case netplay::LockstepSession::CONNECTING: return 1;
    case netplay::LockstepSession::LOBBY: return 2;
    case netplay::LockstepSession::RUNNING: return 3;
    case netplay::LockstepSession::BETWEEN_ROUNDS: return 4;
    case netplay::LockstepSession::ENDED: return 5;
  }
  return 0;
}

// The host's Start, or a peer's Ready (what pressing the lobby's main button does).
EMSCRIPTEN_KEEPALIVE int bitriot_net_press_start()
{
  if (!s_testMode || s_quit || !s_engine || s_engine->netGame() == nullptr)
  {
    return 0;
  }
  EventHandlerNetLobby::requests().startMatch = true;
  return 1;
}

// The rounds played so far, in bitriot_headless's words: "checkpoint R T 0xHASH" and "round R: over at tick T, winner ...".
EMSCRIPTEN_KEEPALIVE const char* bitriot_net_log()
{
  s_returned = (s_testMode && !s_quit && s_engine) ? s_engine->netLog() : std::string();
  return s_returned.c_str();
}

// The session's own account: input delay, how many pump gaps it forgave, how long the host has been quiet, the end reason text.
EMSCRIPTEN_KEEPALIVE const char* bitriot_net_info()
{
  NetGame* net = (s_testMode && !s_quit && s_engine) ? s_engine->netGame() : nullptr;
  if (net == nullptr)
  {
    s_returned = "{}";
  }
  else
  {
    const netplay::LockstepSession& se = net->session();
    s_returned = "{\"delay\":" + std::to_string(se.delay()) + ",\"round\":" + std::to_string(se.round())
               + ",\"tick\":" + std::to_string(se.nextStepTick()) + ",\"sparseGaps\":" + std::to_string(se.sparsePumpGaps())
               + ",\"hostSilenceMs\":" + std::to_string(se.hostSilenceMs())
               + ",\"relayRttMs\":" + std::to_string(net->relayRttMs())
               + ",\"endReason\":" + std::to_string((int) se.endReason())
               + ",\"endText\":\"" + jsonText(se.endText()) + "\",\"stall\":\"" + jsonText(net->stallText()) + "\"}";
  }
  return s_returned.c_str();
}
#endif

// Brief 12 item 4: replays the golden match record (tests/data/golden-round.brmr, preloaded at the same path in a build with
// BITRIOT_WEB_TEST_FILES) through the game's own Simulation and says how it went, as JSON: status 0 is OK (every checkpoint
// hash of the record, which was made natively, matched).
EMSCRIPTEN_KEEPALIVE const char* bitriot_replay_golden()
{
  if (!s_testMode || s_quit || !s_engine)
  {
    s_returned = "{}";
    return s_returned.c_str();
  }
  const ReplayResult r = s_engine->replayRecordFile("/tests/data/golden-round.brmr");
  char hash[32];
  std::snprintf(hash, sizeof(hash), "0x%016llx", (unsigned long long) r.finalHash);
  char expected[32], actual[32];
  std::snprintf(expected, sizeof(expected), "0x%016llx", (unsigned long long) r.expectedHash);
  std::snprintf(actual, sizeof(actual), "0x%016llx", (unsigned long long) r.actualHash);
  std::string message;
  for (char c : r.message) message += (c == '"' || c == '\\' || (unsigned char) c < 0x20) ? ' ' : c;
  s_returned = "{\"status\":" + std::to_string((int) r.status) + ",\"ticks\":" + std::to_string(r.ticksPlayed)
             + ",\"checkpoints\":" + std::to_string(r.checkpointsChecked) + ",\"finalHash\":\"" + hash
             + "\",\"roundOver\":" + (r.roundOver ? "true" : "false") + ",\"mismatchTick\":" + std::to_string(r.mismatchTick)
             + ",\"expectedHash\":\"" + expected + "\",\"actualHash\":\"" + actual + "\",\"message\":\"" + message + "\"}";
  return s_returned.c_str();
}

// 1 if any of the dev knobs (?relay_wt=, ?relay_cert=, ?impair=, ?hiddendriver=) took effect: always 0 on a page without ?test=1.
EMSCRIPTEN_KEEPALIVE int bitriot_dev_knobs_active()
{
  return s_devKnobs ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int bitriot_hidden_polls()
{
  return s_hiddenPolls;
}

EMSCRIPTEN_KEEPALIVE int bitriot_is_hidden()
{
  return s_hidden ? 1 : 0;
}

} // extern "C"

#endif /* __EMSCRIPTEN__ */
