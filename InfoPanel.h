
#ifndef _INFOPANELCLASS
#define _INFOPANELCLASS

#include <assert.h>

#include "EntityFactory.h"

class InfoPanel
{
public:
	InfoPanel(int numPlayers, bool * gender, LPDIRECTDRAW7 lpdd);
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

	void renderSurfaceTo(LPDIRECTDRAWSURFACE7 dest, int x, int y);

private:
	LPDIRECTDRAWSURFACE7 m_surface;
	LPDIRECTDRAWSURFACE7 m_faceSurfaces[4];

	void drawTextGDI(const char * text, int x, int y, 
				 COLORREF color, LPDIRECTDRAWSURFACE7 dest);

	void drawBGTiles();

	int m_players;
	bool m_playerDead[4];
	EntityType playerSelection[4];
	PlayerCharacterEntity * playerPointer[4]; // pointers to player character entities

};

#endif