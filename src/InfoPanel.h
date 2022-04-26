
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

    void renderSurfaceTo(SDL_Surface* dest, int x, int y);

  // TODO change arrays to vectors and resize based on numPlayers
  private:
    SDL_Surface* m_surface;
    SDL_Surface* m_faceSurfaces[GameSettings::MAX_PLAYERS];

    void drawTextGDI(const char * text, int x, int y,
                     Uint32 color, SDL_Surface* dest);

    void drawBGTiles();

    vector<shared_ptr<PlayerCharacterEntity>> m_playerPointers; // pointers to player character entities
    EntityType playerSelection[GameSettings::MAX_PLAYERS];
    

};

#endif
