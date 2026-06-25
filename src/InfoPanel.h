
#ifndef _INFOPANELCLASS
#define _INFOPANELCLASS

#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>


#include "EntityFactory.h"
#include "GameSettings.h"
#include "PlayerCharacterEntity.h"

class InfoPanel
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

    void clearSelections();
    void setSelection(EntityType type, int team);

    // Phase 2.7: mid-game CPU-takeover join indicator. Drawn to the right of the
    // targeted player's portrait. NOTREADY while the joiner is choosing a slot,
    // READY once they have taken the character over (persists as a
    // "device-controlled" badge for the rest of the round).
    enum JoinState { JOIN_NONE, JOIN_NOTREADY, JOIN_READY };
    void setJoinIndicator(int team, JoinState state); // team is 1-based

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

    static SDL_Surface* loadScaledIcon(const char* path, int w, int h);

    void drawTextGDI(const char * text, int x, int y,
                     Uint32 color, SDL_Surface* dest);

    void drawBGTiles();

    vector<shared_ptr<PlayerCharacterEntity>> m_playerPointers; // pointers to player character entities
    EntityType playerSelection[GameSettings::MAX_PLAYERS];
    

};

#endif
