
#ifndef _INFOPANELCLASS
#define _INFOPANELCLASS

#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>


#include "EntityFactory.h"

class InfoPanel
{
public:
	InfoPanel(SDL_Surface* backbuf, int numPlayers, bool * gender);
	~InfoPanel();

	static const int PANEL_TILE_WIDTH = 7;
	static const int WIDTH = PANEL_TILE_WIDTH * Map::TILE_WIDTH;
	static const int HEIGHT = Map::MAP_HEIGHT * Map::TILE_HEIGHT;

    void setPlayerPointers(int num, PlayerCharacterEntity * player1,
		PlayerCharacterEntity * player2,
		PlayerCharacterEntity * player3,
		PlayerCharacterEntity * player4);

	void clearSelections();
	void setSelection(EntityType type, int team);

	void setPlayerDead(int player, bool flag);

	void renderSurfaceTo(SDL_Surface* dest, int x, int y);

private:
	SDL_Surface* m_surface;
	SDL_Surface* m_faceSurfaces[4];

	void drawTextGDI(const char * text, int x, int y, 
				 Uint32 color, SDL_Surface* dest);

	void drawBGTiles();

	int m_players;
	bool m_playerDead[4];
	EntityType playerSelection[4];
	PlayerCharacterEntity * playerPointer[4]; // pointers to player character entities

};

#endif
