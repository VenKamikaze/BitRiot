#include "WorldRenderer.h"

#include "DynamicMap.h"
#include "EntityManager.h"
#include "EntityRendererFactory.h"
#include "MapRenderer.h"

namespace WorldRenderer
{

bool renderEntity(GameEntity& entity, SDL_Surface* dest)
{
  EntityRenderer * er = EntityRendererFactory::getInstance()->getEntityRenderer(entity.getType());

  int destX = entity.pixelX();
  int destY = entity.pixelY();

  // Render in the entity's alliance-team colour (teammates share a colour), not
  // its per-player slot colour. getTeamNumber() -> the same colour-table index.
  return er->render(dest, destX, destY, entity.animState(), entity.animFrame(), entity.getTeamNumber());
}

void renderWorld(EntityManager& entities, SDL_Surface* destination)
{
  // first render static map, then dynamic map
  MapRenderer::getInstance()->render(destination, 0, 0);

  DynamicMap* dynamicMap = entities.getDynamicMap();

  // just iterate through entity list
  // first render static entities, then non-static ones
  for (auto it = dynamicMap->entityList.begin(); it != dynamicMap->entityList.end(); ++it)
  {
    if (it->second->getStaticFlag() == true)
    {
      renderEntity(*it->second, destination);
    }
  }
  for (auto it = dynamicMap->entityList.begin(); it != dynamicMap->entityList.end(); ++it)
  {
    if (it->second->getStaticFlag() == false)
    {
      renderEntity(*it->second, destination);
    }
  }
}

} // namespace WorldRenderer
