
#include "InfoPanel.h"
#include "GameSettings.h"
#include <cstddef>

static const unsigned int TRANSPARENT_COLOR = (255 << 8) + (255 << 0);

int InfoPanel::WIDTH =PANEL_TILE_WIDTH * Map::TILE_WIDTH;
int InfoPanel::HEIGHT=13 * Map::TILE_HEIGHT; //unknown here

bool InfoPanel::DRAW_HEALTH_UNDER_PLAYERS = true;


InfoPanel::InfoPanel(SDL_Surface* backbuf, int numPlayers, std::vector<bool>* malePlayers)
{
  WIDTH = PANEL_TILE_WIDTH * Map::TILE_WIDTH;
  HEIGHT = GameSettings::getInstance()->getMapHeight() * Map::TILE_HEIGHT;

  if(numPlayers > GameSettings::MAX_PLAYERS)
  {
    numPlayers = GameSettings::MAX_PLAYERS;
  }
  if(numPlayers < GameSettings::MIN_PLAYERS)
  {
    numPlayers = GameSettings::MIN_PLAYERS;
  }

  m_playerPointers.resize(numPlayers, nullptr);

  clearSelections();

  m_surface = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, backbuf->format->BitsPerPixel, backbuf->format->Rmask,
      backbuf->format->Gmask, backbuf->format->Bmask, 0);

  // set up face surfaces
  for (int i = 0; i < numPlayers; i++)
  {
    // TODO handle more than 4 players
    stringstream ss;
    ss << "assets/bitmaps/player" << (i + 1);
    if (malePlayers->at(i) == true)
    {
      ss << "male";
    }
    else
    {
      ss << "female";
    }
    ss << "face.bmp";

    m_faceSurfaces[i] = SDL_LoadBMP(ss.str().c_str());
  }

  // change to team colours and apply color key
  for (int i = 0; i < numPlayers; ++i)
  {
    SDL_SetColorKey( m_faceSurfaces[i], SDL_TRUE, SDL_MapRGB(m_faceSurfaces[i]->format, 0, 255, 255) );
  }
}

InfoPanel::~InfoPanel()
{
  if (m_surface)
  {
    SDL_FreeSurface(m_surface);
    m_surface = nullptr;
  }
  for (unsigned int i = 0; i < m_playerPointers.size(); i++)
  {
    if (m_faceSurfaces[i])
    {
      SDL_FreeSurface(m_faceSurfaces[i]);
      m_faceSurfaces[i] = nullptr;
    }
  }
}

// TODO vector types
void InfoPanel::setPlayerPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters)
{
  m_playerPointers = playerCharacters;
}

void InfoPanel::clearSelections()
{
  for (int t = 0; t < GameSettings::MAX_PLAYERS; t++)
  {
    playerSelection[t] = PLAYER_CHARACTER; // player character used as null selection
  }
}

void InfoPanel::setSelection(EntityType type, int team)
{
  team--;
  if (0 <= team && team < GameSettings::MAX_PLAYERS)
  {
    // ok to set array
    playerSelection[team] = type;
  }
}

void InfoPanel::renderSurfaceTo(SDL_Surface* dest, int x, int y)
{
  assert(m_playerPointers.size() > 0);
  assert(m_playerPointers.at(0));

  drawBGTiles();

  SDL_Rect sourceRect, destRect;
  int xOffset, yOffset;

  for (unsigned int i = 0; i < m_playerPointers.size(); i++)
  {
    int health = 0;
    if (m_playerPointers.at(i) && m_playerPointers.at(i)->isAlive())
    {
      health = m_playerPointers.at(i)->getHealth();
    }
    xOffset = 0;
    yOffset = ((i * 3) + 1) * Map::TILE_HEIGHT;

    // draw face
    sourceRect.y = 0;
    sourceRect.x = 0;
    sourceRect.w = m_faceSurfaces[i]->w;
    sourceRect.h = m_faceSurfaces[i]->h;

    destRect.y = yOffset;
    destRect.h = yOffset + (2 * Map::TILE_HEIGHT);
    destRect.x = xOffset;
    destRect.w = xOffset + (2 * Map::TILE_WIDTH);

    //SDL_BlitSurface(m_surface, &sourceRect, m_faceSurfaces[i], &destRect);
    SDL_BlitSurface(m_faceSurfaces[i], &sourceRect, m_surface, &destRect);

    //m_surface->Blt(&destRect, m_faceSurfaces[i], &sourceRect, DDBLT_WAIT | DDBLT_KEYSRC, 0);

    xOffset += (2 * Map::TILE_WIDTH);
    if (playerSelection[i] != PLAYER_CHARACTER)
    {
      EntityRenderer * er = EntityRendererFactory::getInstance()->getEntityRenderer(playerSelection[i]);
      er->render(m_surface, xOffset, yOffset, 0, 0, i + 1);
    }

    // draw health bar
    unsigned char intensity = (unsigned char)(((float)health/100.0f)*255);
    if (health > 100)
    {
      intensity = 255;
    }

    Uint32 fillColour = SDL_MapRGB(m_surface->format, (255-intensity), intensity,0);
    destRect.y = yOffset + Map::TILE_HEIGHT; // + (int)(1.25f * (float)Map::TILE_HEIGHT);
    /*destRect.h = yOffset + (int)(1.75f * (float)Map::TILE_HEIGHT);*/
    destRect.h = Map::TILE_HEIGHT;
    destRect.x = xOffset + (int)(0.25f * (float)Map::TILE_WIDTH);
    destRect.w = health;
    if (health > 100)
    {
      destRect.w = 100;
    }
    SDL_FillRect(m_surface, &destRect, fillColour);
    //m_surface->Blt(&destRect, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);
    // Render the health bar
    if (health > 100)
    {
      int extraHealth = health - 100;
      intensity = (unsigned char)(((float)extraHealth/100.0f)*255);
      //ddbltfx.dwFillColor = ((255 - intensity) << 8) + (intensity << 0);
      fillColour = SDL_MapRGB(m_surface->format, 0, (255-intensity), intensity);

      if (extraHealth>100)
      {
        extraHealth=100;
      }
      destRect.w = extraHealth;

      SDL_FillRect(m_surface, &destRect, fillColour);
      //m_surface->Blt(&destRect, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);
    }


    if (DRAW_HEALTH_UNDER_PLAYERS)
    {
      if (m_playerPointers.at(i) && m_playerPointers.at(i)->isAlive())
      {
        int destX = (m_playerPointers.at(i)->getTileX() * Map::TILE_WIDTH) + (int)(m_playerPointers.at(i)->getOffsetX() * (Map::TILE_WIDTH/2));
        int destY = (m_playerPointers.at(i)->getTileY() * Map::TILE_WIDTH) + (int)(m_playerPointers.at(i)->getOffsetY() * (Map::TILE_WIDTH/2)) + Map::TILE_WIDTH + 2;

        destRect.x = destX;
        destRect.y = destY;

        destRect.h = 3;

        int maxWidth=Map::TILE_WIDTH;
        int width=   ((float)health/100.0f) *maxWidth;
        if (width > maxWidth)
        {
          width = maxWidth;
        }

        Uint32 backColour = SDL_MapRGB(m_surface->format, 0, 0, 0);

        destRect.w = maxWidth;
        SDL_FillRect(dest, &destRect, backColour);

        destRect.w = width;
        SDL_FillRect(dest, &destRect, fillColour);

        // Render the health bar
        if (health > 100)
        {
          int extraHealth = health - 100;
          intensity = (unsigned char)(((float)extraHealth/100.0f)*255);
          //ddbltfx.dwFillColor = ((255 - intensity) << 8) + (intensity << 0);
          fillColour = SDL_MapRGB(m_surface->format, 0, (255-intensity), intensity);

          if (extraHealth>100)
          {
            extraHealth=100;
          }
          destRect.w = extraHealth;

          SDL_FillRect(dest, &destRect, fillColour);
          //m_surface->Blt(&destRect, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);
        }
      }
    }



  }

  /// render surface to destination

  sourceRect.x = 0;
  sourceRect.w = WIDTH;
  sourceRect.y = 0;
  sourceRect.h = HEIGHT;

  destRect.x = x;
  destRect.w = x + WIDTH;
  destRect.y = y;
  destRect.h = y + HEIGHT;

  SDL_BlitSurface(m_surface, &sourceRect, dest, &destRect);
  //dest->Blt(&destRect, m_surface, &sourceRect, DDBLT_WAIT, 0);

}

void InfoPanel::drawTextGDI(const char * text, int x, int y,
                            Uint32 color, SDL_Surface* dest)
{
  /*
  HDC xdc;

  if (FAILED(dest->GetDC(&xdc)))
    return;

  SetTextColor(xdc, color);

  // set background mode to transparent
  SetBkMode(xdc, TRANSPARENT);

  // draw text out
  TextOut(xdc, x, y, text, (int)strlen(text));

  // release device context (or it'd be locked)
  dest->ReleaseDC(xdc);
  */
  static TTF_Font* font = TTF_OpenFont( "assets/bitmaps/FreeMono.ttf", 10 );
  if(!font)
  {
    SDL_Colour colour;
    colour.b = (color << 0);
    colour.g = (color << 8);
    colour.r = (color << 16);
    //Render the text
    SDL_Surface* message = TTF_RenderText_Solid( font, text, colour);

    //If there was an error in rendering the text
    if( message == NULL )
    {
      cout << "Text render failure! Got null message";
    }

    SDL_Rect destRect;
    destRect.x = x;
    destRect.y = y;
    destRect.w = message->w;
    destRect.h = message->h;

    SDL_BlitSurface(message, NULL, dest, &destRect);
    SDL_FreeSurface(message);

    message = NULL;
  }

}

void InfoPanel::drawBGTiles()
{
  for (int y = 0; y < GameSettings::getInstance()->getMapHeight(); y++)
  {
    for (int x = 0; x < InfoPanel::PANEL_TILE_WIDTH; x++)
    {
      Map::StaticTile tile;
      if ((y%3) == 0)
      {
        tile = Map::WALL;
      }
      else
      {
        if (x == InfoPanel::PANEL_TILE_WIDTH - 1)
        {
          tile = Map::WALL;
        }
        else
        {
          tile = Map::EMPTY;
        }
      }

      int atX, atY;
      atX = x * Map::TILE_WIDTH;
      atY = y * Map::TILE_HEIGHT;
      Map::getInstance()->renderTileTo(m_surface, atX, atY, tile);


    }


  }


}
