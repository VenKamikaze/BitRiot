
#include "InfoPanel.h"
#include "GameSettings.h"
#include "EntityRenderer.h"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstddef>

static const unsigned int TRANSPARENT_COLOR = (255 << 8) + (255 << 0);

// Single keyboard asset, recoloured at load time (see makeKeyboardIcon).
const char* InfoPanel::KEYBOARD_ICON_PATH = "assets/menu/keyboard-64w.png";

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

  // Phase 2.7: load the per-slot join indicator icons (scaled to one tile so
  // they sit neatly beside the portrait). Icon number matches the slot it
  // labels, so it always reads as "the controller on this player".
  for (int i = 0; i < GameSettings::MAX_PLAYERS; i++)
  {
    m_joinIndicator[i] = JOIN_NONE;
    m_joinDevice[i] = JOIN_DEVICE_CONTROLLER;
    m_joinIconNotReady[i] = nullptr;
    m_joinIconReady[i] = nullptr;
    m_kbIconTeam[i] = nullptr;
  }
  for (int i = 0; i < numPlayers; i++)
  {
    stringstream notReady, ready;
    notReady << "assets/menu/gamecontroller" << (i + 1) << "-notready.png";
    ready    << "assets/menu/gamecontroller" << (i + 1) << "-ready.png";
    m_joinIconNotReady[i] = loadScaledIcon(notReady.str().c_str(), Map::TILE_WIDTH, Map::TILE_HEIGHT);
    m_joinIconReady[i]    = loadScaledIcon(ready.str().c_str(),    Map::TILE_WIDTH, Map::TILE_HEIGHT);
  }

  // The "selecting a slot" keyboard icon is light gray and state-independent, so
  // build it once here. The team-coloured variants are built lazily at render
  // time: the team-colour table lives on EntityRenderer and is only filled by
  // its constructor, which has not necessarily run yet (the InfoPanel is built
  // before any EntityRenderer). The keyboard is ~1.85:1, so it gets a 2-tile
  // wide box and is scaled proportionally to avoid squashing.
  m_kbIconGray = makeKeyboardIcon(200, 200, 200, 2 * Map::TILE_WIDTH, Map::TILE_HEIGHT);
}

// Load a (PNG) icon and scale it once into a cached surface that alpha-blends
// onto the panel. Returns nullptr on failure (rendering then simply skips it).
SDL_Surface* InfoPanel::loadScaledIcon(const char* path, int w, int h)
{
  SDL_Surface* original = IMG_Load(path);
  if (!original)
  {
    return nullptr;
  }

  SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
  if (!scaled)
  {
    SDL_FreeSurface(original);
    return nullptr;
  }

  // Copy raw RGBA while scaling, then enable blending for the cached surface so
  // the transparent background blends over the panel at blit time.
  SDL_SetSurfaceBlendMode(original, SDL_BLENDMODE_NONE);
  SDL_BlitScaled(original, nullptr, scaled, nullptr);
  SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
  SDL_FreeSurface(original);
  return scaled;
}

// Recolour the single keyboard asset to (r,g,b). The asset is a black-on-white
// silhouette (black frame + key grid, white keys/background), so rather than
// swapping a 3-shade palette like EntityRenderer, treat source luminance as an
// alpha mask: white (the transparency "colour key") -> fully transparent, black
// -> fully opaque target colour, anti-alias greys -> partial alpha. Every pixel
// is painted the target colour, giving a clean tinted keyboard lattice that
// alpha-blends onto the panel. Scaled proportionally to fit maxW x maxH.
SDL_Surface* InfoPanel::makeKeyboardIcon(Uint8 r, Uint8 g, Uint8 b, int maxW, int maxH)
{
  SDL_Surface* original = IMG_Load(KEYBOARD_ICON_PATH);
  if (!original)
  {
    return nullptr;
  }

  SDL_Surface* src = SDL_ConvertSurfaceFormat(original, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(original);
  if (!src)
  {
    return nullptr;
  }

  if (SDL_MUSTLOCK(src))
  {
    SDL_LockSurface(src);
  }
  for (int y = 0; y < src->h; ++y)
  {
    for (int x = 0; x < src->w; ++x)
    {
      Uint8 pr, pg, pb, pa;
      SDL_GetRGBA(SDLHelper::GetPixel(src, x, y), src->format, &pr, &pg, &pb, &pa);
      int luminance = (pr * 30 + pg * 59 + pb * 11) / 100; // 0 (black) .. 255 (white)
      Uint8 alpha = (Uint8)(255 - luminance);
      if (pa < alpha)
      {
        alpha = pa; // never less transparent than the source pixel already was
      }
      SDLHelper::PutPixel(src, x, y, SDL_MapRGBA(src->format, r, g, b, alpha));
    }
  }
  if (SDL_MUSTLOCK(src))
  {
    SDL_UnlockSurface(src);
  }

  double scale = std::min((double) maxW / src->w, (double) maxH / src->h);
  int w = (int)(src->w * scale);
  int h = (int)(src->h * scale);

  SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
  if (!scaled)
  {
    SDL_FreeSurface(src);
    return nullptr;
  }
  SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
  SDL_BlitScaled(src, nullptr, scaled, nullptr);
  SDL_SetSurfaceBlendMode(scaled, SDL_BLENDMODE_BLEND);
  SDL_FreeSurface(src);
  return scaled;
}

// Keyboard tinted to the team base colour of a 0-based slot. The colour comes
// from the same table the player sprites use (EntityRenderer::TEAM_BASE_COLOR,
// indexed by team = slot + 1, matching renderSurfaceTo's er->render(..., i + 1)).
// That table is filled lazily by EntityRenderer's constructor, so this is only
// ever called at render time, by when at least one EntityRenderer exists.
SDL_Surface* InfoPanel::makeTeamKeyboardIcon(int slot)
{
  Uint8 r, g, b;
  SDL_GetRGB(EntityRenderer::TEAM_BASE_COLOR[slot + 1], m_surface->format, &r, &g, &b);
  return makeKeyboardIcon(r, g, b, 2 * Map::TILE_WIDTH, Map::TILE_HEIGHT);
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
  for (int i = 0; i < GameSettings::MAX_PLAYERS; i++)
  {
    if (m_joinIconNotReady[i])
    {
      SDL_FreeSurface(m_joinIconNotReady[i]);
      m_joinIconNotReady[i] = nullptr;
    }
    if (m_joinIconReady[i])
    {
      SDL_FreeSurface(m_joinIconReady[i]);
      m_joinIconReady[i] = nullptr;
    }
    if (m_kbIconTeam[i])
    {
      SDL_FreeSurface(m_kbIconTeam[i]);
      m_kbIconTeam[i] = nullptr;
    }
  }
  if (m_kbIconGray)
  {
    SDL_FreeSurface(m_kbIconGray);
    m_kbIconGray = nullptr;
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

void InfoPanel::setJoinIndicator(int team, JoinState state, JoinDevice device)
{
  team--; // 1-based slot -> 0-based array index (see setSelection)
  if (0 <= team && team < GameSettings::MAX_PLAYERS)
  {
    m_joinIndicator[team] = state;
    m_joinDevice[team] = device;
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

    // Phase 2.7: draw the join indicator icon to the right of the portrait.
    // The health bar lives on the lower portrait row and the item-selection
    // indicator at the inner column, so the far-right of the top row is clear.
    if (m_joinIndicator[i] != JOIN_NONE)
    {
      SDL_Surface* icon = nullptr;
      if (m_joinDevice[i] == JOIN_DEVICE_KEYBOARD)
      {
        // Light gray while choosing a slot; team colour once taken over (also
        // the persistent badge for a human already on keyboard). The team icon
        // is built on first use - see makeTeamKeyboardIcon for the timing.
        if (m_joinIndicator[i] == JOIN_READY)
        {
          if (!m_kbIconTeam[i])
          {
            m_kbIconTeam[i] = makeTeamKeyboardIcon(i);
          }
          icon = m_kbIconTeam[i];
        }
        else
        {
          icon = m_kbIconGray;
        }
      }
      else
      {
        icon = (m_joinIndicator[i] == JOIN_READY) ? m_joinIconReady[i] : m_joinIconNotReady[i];
      }
      if (icon)
      {
        SDL_Rect iconRect;
        iconRect.x = WIDTH - icon->w - 4; // right-aligned; keyboard icon is wider than a tile
        iconRect.y = yOffset + (Map::TILE_HEIGHT / 2);
        iconRect.w = icon->w;
        iconRect.h = icon->h;
        SDL_BlitSurface(icon, nullptr, m_surface, &iconRect);
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
