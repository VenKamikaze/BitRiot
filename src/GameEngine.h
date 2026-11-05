/**
 * GameEngine.h
 * To handle being in gameInit,
 * gameRunning, and GameOver states
 *
 */
#ifndef _GAMEENGINECLASS
#define _GAMEENGINECLASS

#include "EntityManager.h"
#include "InfoPanel.h"
#include "InputHandler.h"
#include "PlayerCharacterEntity.h"
#include "SpawningPool.h"
#include "Simulation.h"
#include "Map.h"
#include <SDL2/SDL.h>

#include "FrameLoop.h"
#include "MatchRecord.h"
#include "MenuSDLRenderer.h"
#include "GameSettings.h"
#include "GameOverTimer.h"
#include "MickSDLRenderer.h"
#ifdef BITRIOT_NETPLAY
#include <deque>
#include "NetGame.h"
#include "net/LobbyModel.h"
#endif

class GameEngine
{
  public:
    GameEngine(SDL_Surface* lpddsback, shared_ptr<InputHandler> inputHandler);
    void setMenuSystem(unique_ptr<MenuSDLRenderer> rmlMenu)
    {
      menuSystem = std::move(rmlMenu);
    }
    ~GameEngine();

    // Write a match record (brief 05) of every round into this directory (one file
    // per round, written when the round ends or is quit). Empty: do not record.
    // Recording only reads the simulation (the inputs it was given, a state hash every
    // 30 ticks); it cannot change a round.
    void setRecordDirectory(const std::string& directory) { m_recordDir = directory; }
    // Writes the round being recorded, if any, and stops recording it. The engine does
    // it when a round ends; main() calls it when the game is closed mid-round.
    void finishRecording();

    // The pieces of a frame, for FrameLoop (see FrameLoop.h for the order they run
    // in). GameEngine no longer has a one-call-does-everything entry point: a frame
    // is steps (zero or more) followed by one render.
    FrameLoop::Hooks frameHooks(std::function<uint32_t()> clockMs);

    // True while ticks may run: a round is in progress and has not just ended.
    bool simulating() const;
    // Reads device and window events (only during a round). A front end that wants
    // its own polling (netplay) should call this too ("wrap, don't replace"): it is
    // what lets renderFrame() skip a redraw when nothing happened and what lets a
    // held-back round still be quit with ESC.
    void pumpEvents();
    // The simulation's tick count: 0 before the first round, then 1 at the first
    // step of a round, counting up. The web build exposes it to JavaScript (the smoke
    // test checks that it advances).
    uint64_t simTick() const { return m_sim.tick(); }
    // Starts a round of `numPlayers` computer-controlled players, as if the Start
    // button had been pressed with every slot set to CPU, without going through the
    // menus. False (and nothing changes) if the count is not a valid player count or the
    // game is not showing a menu. Five or six players raise the map height to the
    // least that shows their portraits (19 for six), as the menu would make you do. Takes effect on the next frame. This is the test hook
    // behind the web build's ?autostart=ai4.
    bool startAllAiMatch(int numPlayers);
    // Forces the next renderFrame() to redraw the world and HUD even if no tick ran
    // and no event was read. Call it when something drawn onto the CPU surface
    // changes outside a tick (netplay's "waiting for <name>" overlay appearing,
    // changing or going away); otherwise the overlay would be composited over the
    // previous frame's text, or left behind.
    void requestRedraw() { m_drawNeeded = true; }
#ifdef BITRIOT_NETPLAY
    // Network play (brief 07). Opens the network, saves the player's settings and shows the
    // lobby page instead of the main menu; false and `error` if it cannot start. Call it
    // after setMenuSystem(). From then on pumpEvents() runs the session every frame, in
    // every state, and canStep()/extraTicks() come from it.
    bool startNetplay(const NetGame::Params& params, std::string* error);
    // Leaves the session (the others are told), restores the player's settings. Safe to
    // call when there is none. main() calls it when the game closes.
    void stopNetplay();
    bool netplayActive() const { return static_cast<bool>(m_net); }
    // The main loop's sleep in a network game: up to `ms`, ending early when a datagram
    // arrives. False when there is no network game (or the backend cannot wait): sleep as usual.
    bool netplayWait(uint32_t ms) { return m_net && m_net->wait(ms); }
#endif

    // One tick: samples the local devices into a TickInputs (the control events
    // produced since the last tick included), hands it to Simulation::step(), buzzes
    // the pads of the players that tick hurt, and notes when it ended the round (or
    // ESC was seen). Does not draw.
    void stepTick();
    // Everything that is not a tick, once per frame: the menu, round set-up, or the
    // world and HUD. Switches to the scoreboard when stepTick() ended the round.
    // Returns false when the game should quit.
    bool renderFrame();


  private:
    SDL_Surface* m_surface = nullptr;
    // The map size the window and back buffer were last made for (the Map's own
    // size follows each round's MatchConfig; the window follows it here).
    int m_windowMapWidth = 0;
    int m_windowMapHeight = 0;
    // Set by stepTick() when the round is over (or ESC was seen): the scoreboard
    // is loaded by the next renderFrame(), after that frame's last draw of the world.
    bool m_roundEnding = false;
    std::shared_ptr<PlayerCharacterEntity> m_scoreboardWinner;
    // Steps run since the last renderFrame() (renderFrame() clears it).
    int m_ticksThisFrame = 0;
    // pumpEvents() ran this frame and read no event (renderFrame() clears it).
    bool m_pumpedQuietly = false;
    // The round was ending when this frame's render began (so its picture is drawn).
    bool m_endingThisFrame = false;
    // The CPU surface holds no picture of the current round yet.
    bool m_drawNeeded = true;
    unique_ptr<MenuSDLRenderer> menuSystem = nullptr;
    std::string m_recordDir;
    std::string m_recordStamp; // UTC date and time the recorded round started
    std::unique_ptr<MatchRecorder> m_recorder; // a round is being recorded
    void startRecording();
#ifdef BITRIOT_NETPLAY
    // -- network play ----------------------------------------------------------------
    std::unique_ptr<NetGame> m_net;
    netplay::RoundStart m_netRound;      // the round the session announced, until resetGame() builds it
    bool m_netRoundPending = false;
    bool m_netEndedShown = false;        // the "disconnected" page is up
    bool m_hostWantsRound = false;       // the host pressed Next round; waiting for the peers to hold the last tick
    bool m_hostWantsLobby = false;       // the host pressed Back to lobby; same wait
    std::string m_netMessage;            // a desync's words (shown over the round and on the error page)
    bool m_netDesyncHandled = false;
    uint32_t m_netDesyncDeadline = 0;
    // The menus' side: a join whose address is being looked up, a failure that has no session to explain it, and
    // the page the error page's button returns to.
    std::unique_ptr<NetResolve> m_netResolve;
    NetGame::Params m_netJoinParams;
    bool m_netLocalErrorShown = false;
    netplay::EndMessage m_netLocalError;
    std::string m_netBackPage = "mainmenu.rml";
    // The overlay over a running round.
    netplay::StallTimer m_netStallTimer;
    std::deque<std::pair<std::string, uint32_t>> m_netToasts;   // text, when it goes
    std::string m_netSlotNames[6];       // who plays each slot (for "<name> left - now AI")
    std::string m_netOverlaySignature;
    void netFrame();
    void netIdleFrame();
    void stepNetTick();
    void resetNetGame();
    void refreshNetPage();
    void showNetLocalError(const netplay::EndMessage& message, const std::string& backPage);
    void leaveNetplayToMenu(const std::string& page);
    void updateNetOverlay();
    void installNetInterceptor();
    void netRoundFinishedHere();
#endif
    // The deterministic world: setup (start) and the per-tick step. Everything the
    // sim needs lives in there; this class keeps rendering, the InfoPanel, sound,
    // the menu and input, and drives the Simulation.
    Simulation m_sim;
    InfoPanel* m_pPanel = nullptr;
    shared_ptr<InputHandler> m_pInputHandler = nullptr;
    //PlayerCharacterEntity* m_pPlayers[NUM_TEAMS];
    vector<shared_ptr<PlayerCharacterEntity>> m_pPlayers;

    // Scoreboard game length: the simulation's tick count x NORMAL_TICKS_SPEED.
    int gameLengthSeconds() const;
    void resetGame();
    // After Simulation::start() has given the Map the round's size: if that differs
    // from the size the window was made for, resize the window, back buffer and
    // texture and reload the tile pictures against the new surface. No-op when the
    // dimensions are unchanged.
    void resizeWindowIfMapChanged();
    // The presentation side of setting up the players the Simulation spawned:
    // which controller drives each, and their team-coloured sprites.
    void initPlayerCharacters(unsigned int numPlayers, std::vector<bool>* malePlayers);
    void uninitialise();
};

#endif
