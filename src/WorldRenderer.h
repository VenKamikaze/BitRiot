// WorldRenderer.h
//
// Draws the simulation's world to an SDL surface. Everything here is read-only
// with respect to the simulation: it looks at entities and the map and never
// changes them, so it can run any number of times per tick (or not at all).
// The simulation library (bitriot_sim) has no drawing code; this is where it
// lives, together with MapRenderer and EntityRenderer.
#ifndef WORLDRENDERER_H_
#define WORLDRENDERER_H_

#include <SDL2/SDL.h>

class EntityManager;
class GameEntity;

namespace WorldRenderer
{

// The static map, then static entities (blocks, eggs, mines ...), then the
// moving ones, in entity-id order within each group.
void renderWorld(EntityManager& entities, SDL_Surface* destination);

// One entity at its pixel position, in its alliance team's colour. Returns false
// if there was nothing to draw (a frame outside the sprite sheet).
bool renderEntity(GameEntity& entity, SDL_Surface* destination);

} // namespace WorldRenderer

#endif /* WORLDRENDERER_H_ */
