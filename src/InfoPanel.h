
#ifndef _INFOPANELCLASS
#define _INFOPANELCLASS

#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>


#include "EntityFactory.h"
#include "GameSettings.h"
#include "PlayerCharacterEntity.h"

class Simulation;

// Where the mid-game join selector shows what it is doing. InfoPanel is the real one;
// a test installs a recorder (InputHandler::setPointers takes the interface), because
// InfoPanel needs a surface and fonts.
class JoinIndicatorView
{
  public:
    virtual ~JoinIndicatorView() {}

    // Phase 2.7: mid-game CPU-takeover join indicator. NOTREADY while the joiner is
    // choosing a slot, READY once they have taken the character over (persists as a
    // "device-controlled" badge for the rest of the round).
    enum JoinState { JOIN_NONE, JOIN_NOTREADY, JOIN_READY };

    // Which kind of device owns the indicator. Controllers keep the existing
    // per-slot gamecontroller PNGs; keyboards use the single recoloured
    // keyboard asset (see makeKeyboardIcon).
    enum JoinDevice { JOIN_DEVICE_CONTROLLER, JOIN_DEVICE_KEYBOARD };

    virtual void setJoinIndicator(int team, JoinState state,
                                  JoinDevice device = JOIN_DEVICE_CONTROLLER) = 0; // team is 1-based
};

class InfoPanel : public JoinIndicatorView
{
  public:
    InfoPanel(SDL_Surface* backbuf, int numPlayers, std::vector<bool>* malePlayers);
    ~InfoPanel();

    static const int PANEL_TILE_WIDTH = 7;
    //static const int WIDTH = PANEL_TILE_WIDTH * Map::TILE_WIDTH;
    //static const int HEIGHT = Map::MAP_HEIGHT * Map::TILE_HEIGHT;

    static int WIDTH;
    static int HEIGHT;

    static bool DRAW_HEALTH_UNDER_PLAYERS;

    // TODO change to vector
    void setPlayerPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters);

    // The simulation whose per-slot selection the panel shows (Simulation::heldSelection):
    // a read-only view, set once per round. Without one no selection is shown.
    void setSimulation(const Simulation* sim);

    // The join indicator (JoinIndicatorView, above). Drawn to the right of the
    // targeted player's portrait.
    void setJoinIndicator(int team, JoinState state,
                          JoinDevice device = JOIN_DEVICE_CONTROLLER) override; // team is 1-based

    void renderSurfaceTo(SDL_Surface* dest, int x, int y);

  // TODO change arrays to vectors and resize based on numPlayers
  private:
    SDL_Surface* m_surface;
    SDL_Surface* m_faceSurfaces[GameSettings::MAX_PLAYERS];

    // Phase 2.7: per-slot join indicator icons, scaled to one tile and cached.
    // Indexed by 0-based slot; icon number matches the slot it sits beside.
    SDL_Surface* m_joinIconNotReady[GameSettings::MAX_PLAYERS];
    SDL_Surface* m_joinIconReady[GameSettings::MAX_PLAYERS];
    JoinState m_joinIndicator[GameSettings::MAX_PLAYERS];
    JoinDevice m_joinDevice[GameSettings::MAX_PLAYERS];

    // Keyboard indicator: one source asset recoloured per use. Mirrors
    // EntityRenderer's colour-key idea, but the keyboard is a 2-tone
    // black-on-white silhouette, so it is tinted by luminance->alpha (white ->
    // transparent, black -> opaque colour) rather than swapping a 3-shade
    // palette. Light gray while a keyboard hot-join is choosing a slot; the
    // slot's team colour once taken over (also the persistent "human is on
    // keyboard" badge).
    static const char* KEYBOARD_ICON_PATH;
    SDL_Surface* m_kbIconGray;                            // shared, state-independent
    SDL_Surface* m_kbIconTeam[GameSettings::MAX_PLAYERS]; // per-slot, built lazily

    static SDL_Surface* loadScaledIcon(const char* path, int w, int h);

    // Recolour the keyboard asset to (r,g,b) and scale to fit a maxW x maxH box
    // preserving aspect ratio. Caller owns the returned surface (nullptr on
    // failure). Static: depends only on the file and the target colour.
    static SDL_Surface* makeKeyboardIcon(Uint8 r, Uint8 g, Uint8 b, int maxW, int maxH);
    // Keyboard tinted to slot's team base colour (0-based slot). Read lazily so
    // the team-colour table is populated by then (see implementation).
    SDL_Surface* makeTeamKeyboardIcon(int slot);

    void drawTextGDI(const char * text, int x, int y,
                     Uint32 color, SDL_Surface* dest);

    void drawBGTiles();

    vector<shared_ptr<PlayerCharacterEntity>> m_playerPointers; // pointers to player character entities
    const Simulation* m_sim = nullptr;

};

#endif
