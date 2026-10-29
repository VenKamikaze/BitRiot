// implementation of player character entity class

#include "PlayerCharacterEntity.h"
#include "SimRandom.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <queue>

// Below this health a bot stops laying anything (eggs, blocks, mines, rockets):
// it cannot afford the cost.
static const int MIN_HEALTH_TO_LAY = 15;

// A bot standing in its own bomb's blast must come out of it with at least this
// much health left (the blast alone can take the full EXPLOSION_INITIAL_HEALTH).
static const int MIN_HEALTH_AFTER_OWN_BLAST = 3;

// How long (in AI frames) a bot stays off a pickup it gave up on before it is
// willing to try again.
static const int PICKUP_RETRY_FRAMES = 600;

// AIGoal::m_targetAmount for GOAL_GET_POWERUP: how the pickup is to be reached
static const int ROUTE_WALK = 0;
static const int ROUTE_THROUGH_BLOCKS = 1;

// How many times in a row a bot may set out for the same pickup without getting
// any closer before it concludes the pickup is not really reachable.
static const int MAX_PICKUP_ATTEMPTS = 3;

PlayerCharacterEntity::PlayerCharacterEntity(int uniqueID, int playerNumber, int atX, int atY, unsigned int flags)
{
  // constructor
  m_ID = uniqueID;
  m_playerNumber = playerNumber;

  m_type = PLAYER_CHARACTER;
  m_groupType = CHARACTER_GROUP;

  m_tileX = atX;
  m_tileY = atY;
  m_offsetX = 0;
  m_offsetY = 0;
  m_health = DataReader::getInstance()->getIntFromFile("PLAYER_INITIAL_HEALTH", "data/player.txt");
  setAlive(m_health > 0);
  
  m_velocityModifier = DataReader::getInstance()->getIntFromFile("PLAYER_VELOCITY_MODIFIER", "data/player.txt");
  m_velocity = fixedFromRatio(m_health + 10, m_velocityModifier);

  m_currentAnimState = EntityAnim::IDLE;
  m_currentAnimFrame = 0;
  m_animDelay = DataReader::getInstance()->getIntFromFile("PLAYER_ANIM_DELAY", "data/player.txt");
  m_animCounter = m_animDelay;

  m_IQ = 0;
  m_checkTimer = (SimRandom::sim().nextInt(2) > 0) ? 37 : 67;

  if ((flags & AI_CONTROLLED_BOT_FLAG) > 0)
  {
    m_controlledByAI = true;

    // pause to give human players a little time at beginning of game
    AIInstruction * ins = new AIInstruction(AIInstruction::WAIT, 23 + (SimRandom::sim().nextInt(42)), getType(), 0, 0);
    m_insList.push_front(ins);

    // set initial goals
    //AIGoal * goal1 = new AIGoal(AIGoal::GOAL_SECURE_AREA, targetAmount, tileX, tileY, targetType, targetID);
    //m_goalList.push_back(goal1);
    AIGoal * goal2 = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES, (SimRandom::sim().nextInt(3)) + 2, -1, -1, EGG2, -1);
    m_goalList.push_back(goal2);

    //AIGoal * goal3 = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES, 8, -1, -1, EGG1, -1);
    //m_goalList.push_back(goal3);

  }
  else
  {
    m_controlledByAI = false;
  }
}

PlayerCharacterEntity::~PlayerCharacterEntity()
{
  // destructor

  clearGoalsAndInstructions();

}

void PlayerCharacterEntity::update()
{
  m_velocity = fixedFromRatio(m_health + 10, m_velocityModifier);

  if (m_controlledByAI == true)
  {
    // this is a bot character, here's the AI! :D
    m_aiFrame++;

    // set check surroundings timer to one of two primes
    m_checkTimer--;
    if (m_checkTimer <= 0)
    {
      m_checkTimer = (SimRandom::sim().nextInt(2) > 0) ? 37 : 67;
      checkEnvironment();
    }

    if (m_goalList.empty() && m_insList.empty())
    {
      determineGoals();
    }

    if (m_insList.empty())
    {
      determineInstructionsFromGoal();
    }

    // add AI for important surroundings sensing whateverness

    processNextInstruction();
  }
}

void PlayerCharacterEntity::incAnimFrame()
{
  m_animCounter--;
  if (m_animCounter <= 0)
  {
    m_animCounter = m_animDelay;
    GameEntity::incAnimFrame();
  }

}

void PlayerCharacterEntity::onCollision(EntityType typeCollidedWith,
                                        EntityGroupType groupTypeCollidedWith,
                                        int collidedWithPlayerNumber)
{
  if ((!isTeammatePlayer(collidedWithPlayerNumber) && (
         (groupTypeCollidedWith == CHARACTER_GROUP) ||
         (groupTypeCollidedWith == MONSTER_GROUP) ||
         (groupTypeCollidedWith == PLAYER_SPAWN_GROUP) ||
         (groupTypeCollidedWith == ROBOT_GROUP)))
      || (typeCollidedWith == EXPLOSION))
  {
    m_health--;
    EntityMessageQueue::getInstance()->postCreateMessage(DAMAGE_EFFECT,
        m_playerNumber, m_tileX, m_tileY, 0, m_offsetX, m_offsetY);
    // Brief 04b: no device call from the simulation. The hit is recorded, and the
    // local layer rumbles the controller for this slot after the step
    // (Simulation::hitThisTick).
    m_hitThisTick = true;
  }

  if (typeCollidedWith == ENERGY_DROP)
  {
    // touched an energy drop
    // regain some health
    incHealth(DataReader::getInstance()->getIntFromFile("ENERGY_DROP_BONUS", "data/player.txt"));
    GameSound::playSound(GameSound::ENERGY_PICKUP);
  }

  if (typeCollidedWith == ENPLUS_POWERUP)
  {
    // touched energy POWERUP
    // regain more health
    incHealth(DataReader::getInstance()->getIntFromFile("ENERGY_POWERUP_BONUS", "data/player.txt"));
    GameSound::playSound(GameSound::ENERGY_PICKUP);
  }

  if (typeCollidedWith == IQPLUS_POWERUP)
  {
    // touched IQ POWERUP
    // increase IQ counter
    incIQ(DataReader::getInstance()->getIntFromFile("IQ_POWERUP_BONUS", "data/player.txt"));
    GameSound::playSound(GameSound::ENERGY_PICKUP);
  }

  if (m_health < 0)
  {
    m_health = 0;
    die();
  }

}

bool PlayerCharacterEntity::createEntity(EntityType type)
{
  // calling function should check there are no static entities in this entity's tile
  // called when player character tries to "lay an entity"

  // this could be done a lot more elegantly

  unsigned int flags = 0;

  // remove health
  switch (type)
  {
    case BLOCK:
      m_health -= DataReader::getInstance()->getIntFromFile("BLOCK_COST", "data/player.txt");
      break;
    case BOMB:
      m_health -= DataReader::getInstance()->getIntFromFile("BOMB_COST", "data/player.txt");
      incNumberBombsPlaced();
      break;
    case MINE:
      m_health -= DataReader::getInstance()->getIntFromFile("MINE_COST", "data/player.txt");
      break;
    case ROCKET_SPIN:
      m_health -= DataReader::getInstance()->getIntFromFile("ROCKET_COST", "data/player.txt");
      break;
    case EGG1:
      m_health -= DataReader::getInstance()->getIntFromFile("EGG1_COST", "data/player.txt");
      incNumberEggsPlaced();
      break;
    case EGG2:
      m_health -= DataReader::getInstance()->getIntFromFile("EGG2_COST", "data/player.txt");
      incNumberEggsPlaced();
      break;
    case EGG3:
      m_health -= DataReader::getInstance()->getIntFromFile("EGG3_COST", "data/player.txt");
      incNumberEggsPlaced();
      break;
    case EGG4:
      m_health -= DataReader::getInstance()->getIntFromFile("EGG4_COST", "data/player.txt");
      incNumberEggsPlaced();
      break;
    case EGG5:
      m_health -= DataReader::getInstance()->getIntFromFile("EGG5_COST", "data/player.txt");
      incNumberEggsPlaced();
      flags |= FIRE_ROCKETS;
      break;
    default:
      // illegal type to lay specified
      return false;
  }
  EntityMessageQueue::getInstance()->postCreateMessage(type, m_playerNumber, m_tileX, m_tileY, flags);

  // check health for death
  if (m_health <= 0)
  {
    m_health = 0;
    die();
  }
  return true;

}

void PlayerCharacterEntity::incHealth(int amount)
{
  m_health += amount;
  int MAX_HEALTH = DataReader::getInstance()->getIntFromFile("PLAYER_MAX_HEALTH", "data/player.txt");
  if (m_health > MAX_HEALTH)
  {
    m_health = MAX_HEALTH;
  }
}

void PlayerCharacterEntity::die()
{
  EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
  GameSound::playSound(GameSound::SPLAT);
}

void PlayerCharacterEntity::determineGoals()
{
  // a reachable powerup / energy drop beats everything else
  if (seekPickup())
  {
    return;
  }

  // other goals
  if (m_health < 45)
  {
    AIGoal * g1 = new AIGoal(AIGoal::GOAL_SECURE_AREA, 0, getTileX(), getTileY(), getType(), 0);
    m_goalList.push_back(g1);
  }
  if (m_health < 100)
  {
    AIGoal * g2 = new AIGoal(AIGoal::GOAL_INCREASE_ENERGY, 130, getTileX(), getTileY(), getType(), 0);
    m_goalList.push_back(g2);
    return;
  }
  if (m_health >= 100)
  {
    // create some entities

    // drone or robot
    if (SimRandom::sim().nextInt(2) > 0)
    {
      // tanks
      AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
                                 1 + SimRandom::sim().nextInt(3), getTileX(), getTileY(), EGG5, 0);
      m_goalList.push_back(goal);
    }
    else
    {
      // drones
      AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
                                 1 + SimRandom::sim().nextInt(4), getTileX(), getTileY(), EGG4, 0);
      m_goalList.push_back(goal);

    }
    if (SimRandom::sim().nextInt(2) > 0)
    {
      // wall eater or two
      AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
                                 (SimRandom::sim().nextInt(2)) + 1, getTileX(), getTileY(), EGG3, 0);
      m_goalList.push_back(goal);
    }
    if (SimRandom::sim().nextInt(10) > 3)
    {
      // egg thief?
      AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
                                 (SimRandom::sim().nextInt(2)) + 1, getTileX(), getTileY(), EGG2, 0);
      m_goalList.push_back(goal);
    }


  }
}

bool PlayerCharacterEntity::seekPickup()
{
  AIGameView * view = AIGameView::getInstance();
  GameSettings * settings = GameSettings::getInstance();
  const int mapWidth = settings->getMapWidth();

  // forgive pickups we gave up on long enough ago
  for (map<int, int>::iterator given = m_abandonedPickups.begin();
       given != m_abandonedPickups.end();)
  {
    if (m_aiFrame - given->second >= PICKUP_RETRY_FRAMES)
    {
      m_pickupAttempts.erase(given->first);
      m_abandonedPickups.erase(given++);
    }
    else
    {
      ++given;
    }
  }

  // A bot too weak to lay anything cannot make its own energy, so a pickup is
  // its only way back: look across the whole map for one that restores energy,
  // not just in the neighbourhood.
  static const int RANGE = 4;
  const bool desperate = m_health < MIN_HEALTH_TO_LAY;
  const int range = desperate
                    ? std::max(mapWidth, settings->getMapHeight())
                    : RANGE;

  struct Candidate
  {
    int x, y;
    EntityInfo info;
  };
  vector<Candidate> candidates;

  for (int x = m_tileX - range; x <= m_tileX + range; ++x)
  {
    if (x < 0 || x >= mapWidth)
    {
      continue;
    }
    for (int y = m_tileY - range; y <= m_tileY + range; ++y)
    {
      if (y < 0 || y >= settings->getMapHeight())
      {
        continue;
      }

      if (x == m_tileX && y == m_tileY)
      {
        continue;
      }

      const vector<EntityInfo> & info = view->getEntityInfoAtLocation(x, y);
      vector<EntityInfo>::const_iterator it;

      for (it = info.begin(); it != info.end(); ++it)
      {
        bool wanted;
        if (desperate)
        {
          wanted = (it->type == ENERGY_DROP || it->type == ENPLUS_POWERUP);
        }
        else
        {
          wanted = (it->groupType == POWERUP_GROUP || it->type == ENERGY_DROP);
        }

        if (wanted && m_abandonedPickups.find(it->id) == m_abandonedPickups.end()
            && !pickupIsHazardous(x, y))
        {
          Candidate c = { x, y, *it };
          candidates.push_back(c);
        }
      }
    }
  }

  if (candidates.empty())
  {
    return false;
  }

  // First choice is a pickup we can simply walk to. Only if there is none do we
  // consider ones behind blocks, which means bombing our way through.
  for (int pass = 0; pass < 2; ++pass)
  {
    const bool throughBlocks = (pass == 1);
    if (throughBlocks && !canBombBlocks())
    {
      break;
    }

    // one flood fill classifies every candidate
    vector<RouteInfo> routes;
    floodRoutes(throughBlocks, routes);

    // The reachable ones, nearest first by the cost of the route itself (as the
    // path search weighs it: a block counts as 6 tiles). Ties keep scan order,
    // so the choice is deterministic.
    vector<pair<int, int> > reachable; // (route cost, index into candidates)
    for (size_t i = 0; i < candidates.size(); ++i)
    {
      const RouteInfo & route = routes[candidates[i].y * mapWidth + candidates[i].x];
      if (route.reachable)
      {
        reachable.push_back(pair<int, int>(route.steps + 5 * route.blocks, (int) i));
      }
    }
    std::stable_sort(reachable.begin(), reachable.end(),
                     [](const pair<int, int> & a, const pair<int, int> & b)
                     { return a.first < b.first; });

    vector<pair<int, int> >::iterator r;
    for (r = reachable.begin(); r != reachable.end(); ++r)
    {
      const Candidate & c = candidates[r->second];
      const int id = c.info.id;
      const RouteInfo & route = routes[c.y * mapWidth + c.x];

      // A route through blocks starts with a bomb; if that bomb would be
      // refused when the time came, don't set out on it only to fail.
      if (route.blocks > 0 && !bombSiteAcceptable(route.bombFromX, route.bombFromY))
      {
        continue;
      }

      // Have we set out for this one before and got nowhere? Being picked again
      // with no fewer blocks to bomb and no fewer tiles to walk than last time
      // means the instructions for reaching it keep failing (or being
      // interrupted), so stop trying rather than loop forever.
      PickupAttempt & attempt = m_pickupAttempts[id];
      if (route.blocks < attempt.bestBlocks
          || (route.blocks == attempt.bestBlocks && route.steps < attempt.bestSteps))
      {
        attempt.bestBlocks = route.blocks;
        attempt.bestSteps = route.steps;
        attempt.failures = 0;
      }
      else if (++attempt.failures >= MAX_PICKUP_ATTEMPTS)
      {
        m_abandonedPickups[id] = m_aiFrame;
        continue;
      }

      m_goalList.push_front(new AIGoal(AIGoal::GOAL_GET_POWERUP,
                                       throughBlocks ? ROUTE_THROUGH_BLOCKS : ROUTE_WALK,
                                       c.x, c.y, c.info.type, id));
      return true;
    }
  }

  return false;
}

void PlayerCharacterEntity::floodRoutes(bool allowBlockBombing, vector<RouteInfo> & routes)
{
  // Dijkstra from our tile with the same costs the A* search uses (1 per tile,
  // 5 extra for a block). Ties resolve by (cost, y, x), so it is deterministic.
  const int width = GameSettings::getInstance()->getMapWidth();
  const int height = GameSettings::getInstance()->getMapHeight();
  Map * map = Map::getInstance();

  routes.assign(width * height, RouteInfo());
  vector<int> cost(width * height, -1);

  typedef pair<int, pair<int, int> > Entry; // (cost, (y, x))
  priority_queue<Entry, vector<Entry>, greater<Entry> > frontier;

  const int start = m_tileY * width + m_tileX;
  routes[start].reachable = true;
  cost[start] = 0;
  frontier.push(Entry(0, pair<int, int>(m_tileY, m_tileX)));

  static const int dx[4] = { 1, -1, 0, 0 };
  static const int dy[4] = { 0, 0, 1, -1 };

  while (!frontier.empty())
  {
    const Entry top = frontier.top();
    frontier.pop();
    const int x = top.second.second;
    const int y = top.second.first;
    const int here = y * width + x;
    if (top.first > cost[here])
    {
      continue; // stale queue entry
    }

    for (int i = 0; i < 4; ++i)
    {
      const int nx = x + dx[i];
      const int ny = y + dy[i];
      if (nx < 0 || nx >= width || ny < 0 || ny >= height)
      {
        continue;
      }

      const Map::StaticTile tile = map->staticTileAt(nx, ny);
      if (tile == Map::WALL || (tile != Map::EMPTY && !allowBlockBombing))
      {
        continue;
      }

      const bool block = (tile == Map::BLOCK);
      const int next = ny * width + nx;
      const int nextCost = top.first + 1 + (block ? 5 : 0);
      if (cost[next] == -1 || nextCost < cost[next])
      {
        cost[next] = nextCost;
        routes[next].reachable = true;
        routes[next].steps = routes[here].steps + 1;
        routes[next].blocks = routes[here].blocks + (block ? 1 : 0);
        // where the first block on the way gets bombed from: the tile before it
        if (routes[here].bombFromX >= 0)
        {
          routes[next].bombFromX = routes[here].bombFromX;
          routes[next].bombFromY = routes[here].bombFromY;
        }
        else if (block)
        {
          routes[next].bombFromX = x;
          routes[next].bombFromY = y;
        }
        else
        {
          routes[next].bombFromX = routes[next].bombFromY = -1;
        }
        frontier.push(Entry(nextCost, pair<int, int>(ny, nx)));
      }
    }
  }
}

bool PlayerCharacterEntity::pickupIsHazardous(int tileX, int tileY)
{
  // don't walk onto a pickup that is about to be blown up, is being blown up,
  // or sits on an enemy mine
  const vector<EntityInfo> & info = AIGameView::getInstance()->getEntityInfoAtLocation(tileX, tileY);
  vector<EntityInfo>::const_iterator it;
  for (it = info.begin(); it != info.end(); ++it)
  {
    if (it->type == BOMB || it->type == EXPLOSION
        || (it->type == MINE && !isTeammatePlayer(it->playerNumber)))
    {
      return true;
    }
  }
  return false;
}

bool PlayerCharacterEntity::canBombBlocks()
{
  if (m_health >= MIN_HEALTH_TO_LAY)
  {
    return true;
  }

  // Too weak to lay things in general, but a bomb is the only way out of a
  // walled-in spot. Fine as long as we come out of its cost and blast alive.
  DataReader * data = DataReader::getInstance();
  return m_health - data->getIntFromFile("BOMB_COST", "data/player.txt")
                  - data->getIntFromFile("EXPLOSION_INITIAL_HEALTH", "data/entity.txt")
         >= MIN_HEALTH_AFTER_OWN_BLAST;
}

bool PlayerCharacterEntity::explosivesAdjacent(int tileX, int tileY)
{
  // A bomb's blast (GameEntity::explode) covers its own tile and the four
  // orthogonal neighbours, so only something on those four tiles is set off by
  // it - and what it sets off blasts the bomb's tile again, and whoever stands
  // on it, a second time. Eggs (anyone's), mines and other bombs do that. Energy
  // drops and powerups next to the bomb are no danger to us but are destroyed,
  // and teammates' eggs are lost; safeLocationToBomb() already refuses those.
  static const int dx[4] = { 1, -1, 0, 0 };
  static const int dy[4] = { 0, 0, 1, -1 };
  AIGameView * view = AIGameView::getInstance();

  for (int i = 0; i < 4; ++i)
  {
    const int x = tileX + dx[i];
    const int y = tileY + dy[i];
    if (x < 0 || x >= GameSettings::getInstance()->getMapWidth()
        || y < 0 || y >= GameSettings::getInstance()->getMapHeight())
    {
      continue;
    }

    const vector<EntityInfo> & info = view->getEntityInfoAtLocation(x, y);
    vector<EntityInfo>::const_iterator it;
    for (it = info.begin(); it != info.end(); ++it)
    {
      if (it->groupType == EGG_GROUP || it->type == MINE || it->type == BOMB)
      {
        return true;
      }
    }
  }
  return false;
}

bool PlayerCharacterEntity::bombSiteAcceptable(int tileX, int tileY)
{
  // would a bomb laid here to clear a block be allowed? Same rules as the
  // CREATE_ENTITY instruction applies when it comes to laying it.
  return safeLocationToBomb(tileX, tileY)
         && (m_health >= MIN_HEALTH_TO_LAY || !explosivesAdjacent(tileX, tileY));
}

void PlayerCharacterEntity::determineInstructionsFromGoal()
{
  if (m_goalList.empty())
  {
    return;
  }

  AIGoal * currentGoal = m_goalList.front();
  m_goalList.pop_front();

  switch (currentGoal->m_goalType)
  {
    case AIGoal::GOAL_INCREASE_ENERGY:
      {
        // create 8 energy drops at once

        AIGoal * create = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES, 2 + SimRandom::sim().nextInt(5),
                                     getTileX(), getTileY(), EGG1, 0);
        m_goalList.push_back(create);

        determineInstructionsFromGoal();

        AIInstruction * wait = new AIInstruction(AIInstruction::WAIT,
            33, getType(), 0, 0);
        m_insList.push_back(wait);



      }
      break;
    case AIGoal::GOAL_SECURE_AREA:
      {
        // use path flooding to certain depth and place blocks at end
        /*
        const int SECURE_DISTANCE = 3;

        int x1, x2, y1, y2;
        x1 = m_tileX - SECURE_DISTANCE;
        x2 = m_tileX + SECURE_DISTANCE;
        y1 = m_tileY - SECURE_DISTANCE;
        y2 = m_tileY + SECURE_DISTANCE;

        for (int counter = x1; counter <= x2; ++counter)
        {
          // at (x1, y1 + counter), (x1 + counter, y1)
          // (x2, y1 + counter), (x1 + counter, y2)
          for (int i = 0; i < 4; ++i)
          {
            int xCheck, yCheck;
            switch (i)
            {
            case 0:
              xCheck = x1;
              yCheck = y1 + counter;
              break;
            case 1:
              xCheck = x1 + counter;
              yCheck = y1;
              break;
            case 2:
              xCheck = x2;
              yCheck = y1 + counter;
              break;
            case 3:
              xCheck = x1 + counter;
              yCheck = y2;
              break;
            }

            if (xCheck <= 0 || xCheck >= GameSettings::getInstance()->getMapWidth() - 1)
              continue;
            if (yCheck <= 0 || yCheck >= GameSettings::getInstance()->getMapHeight() - 1)
              continue;

            // is location accessible
            coord next = getNextTileFromPathfinding(xCheck, yCheck);

            if (Map::getInstance()->staticTileAt(xCheck, yCheck) != Map::EMPTY)
              continue; // blocked

            if (next.first == m_tileX && next.second == m_tileY)
              continue; // can't get there

            // else place block there
            AIInstruction * goTo, * place;//, * returnJourney;

            goTo = new AIInstruction(AIInstruction::GO_TO,
              0, getType(), xCheck, yCheck);
            m_insList.push_back(goTo);

            place = new AIInstruction(AIInstruction::CREATE_ENTITY,
              1, BLOCK, xCheck, yCheck);
            m_insList.push_back(place);

            //returnJourney = new AIInstruction(AIInstruction::GO_TO,
            //  0, getType(), next.first, next.second);
            //m_insList.push_back(returnJourney);



          }


        }*/
      }
      break;
    case AIGoal::GOAL_CREATE_ENTITIES:
      {
        // this can be done by just adding instructions
        // to create the right kind and number of eggs

        // the instruction handing will move character as needed
        for (int i = 0; i < currentGoal->m_targetAmount; ++i)
        {
          // add pause to simulate human player decision/action delay
          // could remove for more difficult AI bots
          AIInstruction * waitIns =
            new AIInstruction(AIInstruction::WAIT, 5 + (SimRandom::sim().nextInt(11)), getType(), -1, -1);
          m_insList.push_back(waitIns);


          AIInstruction * ins =
            new AIInstruction(AIInstruction::CREATE_ENTITY, 1, currentGoal->m_targetType, getTileX(), getTileY());
          m_insList.push_back(ins);


        }

      }
      break;
    case AIGoal::GOAL_ATTACK_TEAM:
      {


      }
      break;
    case AIGoal::GOAL_ATTACK_ENTITY:
      {


      }
      break;
    case AIGoal::GOAL_GET_POWERUP:
      {
        if (currentGoal->m_tileX == m_tileX && currentGoal->m_tileY == m_tileY)
        {
          break;  // we're already there
        }

        // check powerup still exists
        vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(
                                    currentGoal->m_tileX, currentGoal->m_tileY);
        vector<EntityInfo>::iterator it;
        bool targetExists = false;
        for (it = info.begin(); it != info.end(); ++it)
        {
          if (it->id == currentGoal->m_targetID)
          {
            targetExists = true;
            break;
          }
        }
        if (!targetExists)
        {
          break;
        }

        // keep this goal for later REMOVE??
        // as we may need multiple block bombings
        //AIGoal * copy = new AIGoal(*currentGoal);
        //m_goalList.push_front(copy);

        // set "final" goto
        AIInstruction * target = new AIInstruction(AIInstruction::GO_TO, 0, currentGoal->m_targetType,
            currentGoal->m_tileX, currentGoal->m_tileY);
        m_insList.push_front(target);

        // go to powerup, with demolition only if the route needs it
        coord next = getNextTileFromPathfinding(currentGoal->m_tileX, currentGoal->m_tileY,
                                                currentGoal->m_targetAmount == ROUTE_THROUGH_BLOCKS);

        // seeing as we've already calculated it...
        AIInstruction * firstGoTo = new AIInstruction(AIInstruction::GO_TO,
            0, currentGoal->m_targetType, next.first, next.second);
        m_insList.push_front(firstGoTo);


      }
      break;
  }

  delete currentGoal;
}

void PlayerCharacterEntity::processNextInstruction()
{
  if (m_insList.empty())
  {
    return;
  }

  AIInstruction * ins = m_insList.front();
  m_insList.pop_front();

  switch (ins->m_type)
  {
    case AIInstruction::CREATE_ENTITY:
      {
        if (m_health < MIN_HEALTH_TO_LAY
            && !(ins->m_targetType == BOMB && canBombBlocks()
                 && !explosivesAdjacent(m_tileX, m_tileY)))
        {
          break;
        }
        // check if this tile already has a static entity
        bool staticAtLocation = this->staticEntityAtLocation(getTileX(), getTileY());

        if (staticAtLocation)
        {
          // there is already a static entity

          // if this is an egg type required, move to nearest "empty" tile
          if (   ins->m_targetType == EGG1
                 || ins->m_targetType == EGG2
                 || ins->m_targetType == EGG3
                 || ins->m_targetType == EGG4
                 || ins->m_targetType == EGG5 )
          {
            // find nearest empty tile and move there, replace instruction

            if (goToNearestEmptyTile(ins->m_targetTileX, ins->m_targetTileY))
            {
              // can go to an empty tile
              AIInstruction * copiedIns =
                new AIInstruction(*ins);

              // put CREATE instruction after GOTO at head of list
              AIInstruction * goToIns = m_insList.front();
              m_insList.pop_front();
              m_insList.push_front(copiedIns);
              m_insList.push_front(goToIns);
            }
            else
            {
              // failed to find a nearby empty tile

              // forget this instruction for now

            }
          }
          else
          {
            // is a weapon/block type
            // probably not very helpful to move location
            // so forget this instruction

          }


        }
        else
        {
          // there isn't a static entity

          // if this is an egg, is it a safe enough location?

          // is this a weapon? if so, check desired spot
          if (ins->m_targetType == BOMB ||
              ins->m_targetType == BLOCK ||
              ins->m_targetType == MINE ||
              ins->m_targetType == ROCKET_SPIN)
          {
            if (ins->m_targetType == BOMB ||
                ins->m_targetType == MINE)
            {
              if (!safeLocationToBomb(m_tileX, m_tileY))
              {
                break;
              }
            }

            if (ins->m_targetTileX == m_tileX
                && ins->m_targetTileY == m_tileY)
            {
              createEntity(ins->m_targetType);
            }
          }
          else
          {
            // check this location is safe for an egg

            bool bombOrMineFound = false;

            for (int i = 0; i < 4; ++i)
            {
              int x, y;
              x = m_tileX;
              y = m_tileY;

              switch (i)
              {
                case 0:
                  x++;
                  break;
                case 1:
                  x--;
                  break;
                case 2:
                  y++;
                  break;
                case 3:
                  y--;
                  break;
              }

              vector<EntityInfo> eiVec = AIGameView::getInstance()->getEntityInfoAtLocation(x, y);
              vector<EntityInfo>::iterator it;

              for (it = eiVec.begin(); it != eiVec.end(); ++it)
              {
                // even our team's bombs and mines are dangerous to eggs
                if (it->type == BOMB || it->type == MINE)
                {
                  bombOrMineFound = true;
                  break;
                }

              }

              if (bombOrMineFound)
              {
                break;
              }

            } // end for 0..3

            if (!bombOrMineFound)
            {
              createEntity(ins->m_targetType);
            }
            else
            {
              // move elsewhere
              AIInstruction * copy = new AIInstruction(*ins);
              m_insList.push_front(copy);

              goNumTilesAway(3);
            }
          }
        }


      }
      break;
    case AIInstruction::GO_TO:
      {
        // check for simple 0 or 1 tile movement
        int xDist, yDist;
        xDist = m_tileX - ins->m_targetTileX;
        yDist = m_tileY - ins->m_targetTileY;

        bool moved = false;


        if (xDist == 0 && yDist == 0)
        {
          // in target tile, close enough to center?
          if (nearTileCenter())
          {
            // finished this instruction
            break;
          }
          else
          {
            // keep moving in current direction
            DIRECTION dir = static_cast<DIRECTION>(m_currentAnimState - 1);
            moveInDirection(dir);
            moved = true;
          }
        }

        // only one tile to go?

        // check target tile isn't blocked
        if (Map::getInstance()->staticTileAt(ins->m_targetTileX, ins->m_targetTileY) != Map::EMPTY)
        {
          break;
        }

        if (xDist == -1 && yDist == 0)
        {
          moveInDirection(RIGHT);
          moved = true;
        }
        else if (xDist == 1 && yDist == 0)
        {
          moveInDirection(LEFT);
          moved = true;
        }
        else if (xDist == 0 && yDist == 1)
        {
          moveInDirection(UP);
          moved = true;
        }
        else if (xDist == 0 && yDist == -1)
        {
          moveInDirection(DOWN);
          moved = true;
        }



        if (moved)
        {
          // copy instruction
          AIInstruction * copyIns = new AIInstruction(*ins);
          m_insList.push_front(copyIns);

          incAnimFrame();
          break;
        }


        // there is a multiple tile distance, use path finding

        // get next tile by pathfinding
        coord next = getNextTileFromPathfinding(ins->m_targetTileX, ins->m_targetTileY);
        // did pathfinding work?
        if (next.first == getTileX() && next.second == getTileY())
        {
          // did not work, or we're already where we want to be


        }
        else
        {
          // copy instruction
          AIInstruction * copyIns = new AIInstruction(*ins);
          m_insList.push_front(copyIns);

          // add this tile as goto
          AIInstruction * newIns = new AIInstruction(AIInstruction::GO_TO,
              0, getType(), next.first, next.second);
          m_insList.push_front(newIns);
          processNextInstruction();
        }

      }
      break;
    case AIInstruction::MOVE_FROM_LOCATION:
      {
        goNumTilesAway(ins->m_targetAmount);
      }
      break;
    case AIInstruction::WAIT:
      {
        int waitAmount = ins->m_targetAmount - 1;
        if (waitAmount > 0)
        {
          AIInstruction * newIns = new AIInstruction(ins->m_type,
              waitAmount, ins->m_targetType, ins->m_targetTileX, ins->m_targetTileY);
          m_insList.push_front(newIns);
        }
        // else nothing more to do
      }
      break;
  }

  delete ins;
}

bool PlayerCharacterEntity::staticEntityAtLocation(int tileX, int tileY)
{
  bool staticAtLocation = false;

  AIGameView * view = AIGameView::getInstance();
  vector<EntityInfo> info = view->getEntityInfoAtLocation(tileX, tileY);

  vector<EntityInfo>::iterator it;
  for (it = info.begin(); it != info.end(); ++it)
  {

    staticAtLocation = it->staticFlag;

    if (staticAtLocation)
    {
      break;
    }
  }

  return staticAtLocation;
}

bool PlayerCharacterEntity::goToNearestEmptyTile(int fromX, int fromY)
{
  // set an immediate GO_TO instruction if a suitable empty tile is found
  set<coord> searched;
  list<coord> toSearch;

  toSearch.push_front(coord(fromX, fromY));

  while (!toSearch.empty())
  {
    // get head of toSearch
    coord currentLocation = toSearch.front();
    toSearch.pop_front();

    // is this an empty tile we want?
    if (!staticEntityAtLocation(currentLocation.first, currentLocation.second))
    {
      // it is, set GO_TO instruction and be happy
      AIInstruction * ins = new AIInstruction(AIInstruction::GO_TO,
                                              0, getType(), currentLocation.first, currentLocation.second);
      m_insList.push_front(ins);

      return true;
    }
    else
    {
      // there is a static entity here, add valid adjoining tiles to toSearch list

      searched.insert(currentLocation);

      Map * staticMap = Map::getInstance();

      for (int i = 0; i < 4; ++i)
      {
        int x, y;
        x = currentLocation.first;
        y = currentLocation.second;

        switch (i)
        {
          case 0:
            x++;
            break;
          case 1:
            x--;
            break;
          case 2:
            y++;
            break;
          case 3:
            y--;
            break;
        }

        if (staticMap->staticTileAt(x, y) == Map::EMPTY)
        {
          coord newLocation(x, y);
          // check searched set
          if (searched.find(newLocation) == searched.end())
          {
            toSearch.push_back(newLocation);
          }
        }

      } // end for 0..3

    } // end else static entry found

  } // end while toSearch not-empty

  return false;
}

bool PlayerCharacterEntity::goNumTilesAway(int numToMove)
{
  // set an immediate GO_TO instruction if a suitable tile is found
  set<coord> searched;
  list<pair<int,coord> > toSearch;

  toSearch.push_front(pair<int, coord>(0, coord(getTileX(), getTileY())));

  while (!toSearch.empty())
  {
    // get head of toSearch
    pair<int, coord> currentLocation = toSearch.front();
    toSearch.pop_front();

    // is this the tile we want?
    if (currentLocation.first >= numToMove)
    {
      // it is, set GO_TO instruction and be happy
      AIInstruction * ins = new AIInstruction(AIInstruction::GO_TO,
                                              0, getType(), currentLocation.second.first, currentLocation.second.second);
      m_insList.push_front(ins);

      return true;
    }
    else
    {
      // add valid adjoining tiles to toSearch list

      searched.insert(currentLocation.second);

      Map * staticMap = Map::getInstance();
      AIGameView * view = AIGameView::getInstance();

      for (int i = 0; i < 4; ++i)
      {
        int x, y;
        x = currentLocation.second.first;
        y = currentLocation.second.second;

        switch (i)
        {
          case 0:
            x++;
            break;
          case 1:
            x--;
            break;
          case 2:
            y++;
            break;
          case 3:
            y--;
            break;
        }

        if (staticMap->staticTileAt(x, y) == Map::EMPTY)
        {
          // check for hostile mines
          bool badMineFound = false;

          vector<EntityInfo> info = view->getEntityInfoAtLocation(x, y);
          vector<EntityInfo>::iterator it;
          for (it = info.begin(); it != info.end(); ++it)
          {
            if (it->type == MINE && !isTeammatePlayer(it->playerNumber))
            {
              badMineFound = true;
              break;
            }
          }

          if (badMineFound)
          {
            continue;
          }

          coord newCoord(x, y);
          pair<int, coord> newLocation(currentLocation.first + 1, newCoord);
          // check searched set
          if (searched.find(newCoord) == searched.end())
          {
            toSearch.push_back(newLocation);
          }
        }

      } // end for 0..3

    } // end else static entry found

  } // end while toSearch not-empty

  return false;
}

bool PlayerCharacterEntity::nearTileCenter()
{
  const fixed_t absOffsetX = fixedAbs(m_offsetX);
  const fixed_t absOffsetY = fixedAbs(m_offsetY);

  if ((absOffsetX <= m_velocity) && (absOffsetY <= m_velocity))
  {
    return true;
  }
  else
  {
    return false;
  }
}

// Path search nodes alive right now, across every search (see livePathNodes()).
static long s_livePathNodes = 0;

struct Node
{
  Node() : heuristic(0), cost(0), parent(NULL) { ++s_livePathNodes; }
  ~Node() { --s_livePathNodes; }

  coord location;
  int heuristic;
  int cost;

  Node * parent;

private:
  Node(const Node &);
  Node & operator=(const Node &);
};

long PlayerCharacterEntity::livePathNodes()
{
  return s_livePathNodes;
}

static long s_pathSearches = 0;

long PlayerCharacterEntity::pathSearchesRun()
{
  return s_pathSearches;
}

coord PlayerCharacterEntity::getNextTileFromPathfinding(int targetTileX, int targetTileY,
    bool allowBlockBombing)
{
  // pretty standard A* pathfinding
  ++s_pathSearches;

  // Every node is owned here, so all of them are freed however we leave. The
  // closed and open maps only point into it. (Nodes used to be deleted by hand
  // and several paths missed: replaced duplicates, the goal node, anything
  // still in flight when the goal was found.)
  vector<std::unique_ptr<Node> > nodes;
  struct NodeMaker
  {
    vector<std::unique_ptr<Node> > & nodes;
    Node * make(int x, int y, int heuristic, int cost, Node * parent)
    {
      nodes.push_back(std::unique_ptr<Node>(new Node));
      Node * n = nodes.back().get();
      n->location = coord(x, y);
      n->heuristic = heuristic;
      n->cost = cost;
      n->parent = parent;
      return n;
    }
  } maker = { nodes };

  map<coord, Node *> closed; // previously explored nodes
  // Nodes to be explored, in the order they are tried: cost + heuristic first,
  // and among equal totals the lowest tie-break number. A node whose total is
  // already taken by another gets the next free number, counting up from 0, as
  // it used to get the next free +0.001 on a float key - so the order is the
  // same as it always was, including a slot freed by a node leaving the list
  // being reused by the next arrival with that total.
  // The equivalence relies on fewer than 1000 nodes ever sharing one total, as 1000
  // nudges of 0.001 would have reached the next whole total. The list holds each
  // location at most once (a better path replaces, an equal or worse one is
  // dropped), so it never holds more nodes than the map has tiles: 825 on the
  // largest map the menu allows (33x25), far fewer in practice.
  typedef pair<int, int> OpenKey;
  map<OpenKey, Node *> open;

  coord result(m_tileX, m_tileY);

  // set initial node
  Node * initial = maker.make(m_tileX, m_tileY,
                              abs(m_tileX - targetTileX) + abs(m_tileY - targetTileY), 0, NULL);

  // add to open
  open.insert(pair<OpenKey, Node *>(OpenKey(initial->heuristic, 0), initial));

  while (!open.empty())
  {
    // get best node of open list
    Node * current = open.begin()->second;
    open.erase(open.begin());

    // is this the goal node?
    if (current->location.first == targetTileX && current->location.second == targetTileY)
    {
      // get the result we want

      // may be blocks on solution path need bombing
      int lastPreBlockX = -1;
      int lastPreBlockY = -1;
      bool blockEncountered = false;

      Node * walker = current;

      while (walker->parent != initial)
      {
        if (Map::getInstance()->staticTileAt(walker->location.first, walker->location.second)
            == Map::BLOCK)
        {
          blockEncountered = true;
        }

        if (walker->parent == NULL)
        {
          break;
        }
        walker = (walker->parent);

        if (blockEncountered)
        {
          blockEncountered = false;
          lastPreBlockX = walker->location.first;
          lastPreBlockY = walker->location.second;
        }
      }

      // The loop above stops at the first step without looking at it, so a
      // block right beside us was never noticed. It has to be bombed from the
      // tile we are standing on.
      if (walker->parent == initial
          && Map::getInstance()->staticTileAt(walker->location.first, walker->location.second)
          == Map::BLOCK)
      {
        lastPreBlockX = m_tileX;
        lastPreBlockY = m_tileY;
      }

      if (lastPreBlockX != -1)
      {
        // we've got some bombing to do

        AIInstruction * goToIns, * bombIns, * duckAndCoverIns, * waitIns;

        goToIns = new AIInstruction(
          AIInstruction::GO_TO, 0, getType(), lastPreBlockX, lastPreBlockY);
        bombIns = new AIInstruction(
          AIInstruction::CREATE_ENTITY, 1, BOMB,  lastPreBlockX, lastPreBlockY);
        duckAndCoverIns = new AIInstruction(
          AIInstruction::MOVE_FROM_LOCATION, 2, getType(), lastPreBlockX, lastPreBlockY);
        waitIns = new AIInstruction(
          AIInstruction::WAIT, 45 /* base decay / 2 */, getType(), lastPreBlockX, lastPreBlockY);

        m_insList.push_front(waitIns);
        m_insList.push_front(duckAndCoverIns);
        m_insList.push_front(bombIns);
        m_insList.push_front(goToIns);

      }
      result.first = walker->location.first;
      result.second = walker->location.second;
      break;
    }
    else
    {
      // not the goal node

      // generate possible child nodes
      for (int i = 0; i < 4; ++i)
      {
        int x, y;
        x = current->location.first;
        y = current->location.second;
        switch (i)
        {
          case 0:
            x++;
            break;
          case 1:
            x--;
            break;
          case 2:
            y++;
            break;
          case 3:
            y--;
            break;
        }

        // is this a valid location to move to? if not, skip this location
        if ((Map::getInstance()->staticTileAt(x, y) != Map::EMPTY && !allowBlockBombing)
            || (Map::getInstance()->staticTileAt(x, y) == Map::WALL))
        {
          continue;
        }

        // ok to move here, work out the node data
        int cost = current->cost + 1;
        if (Map::getInstance()->staticTileAt(x, y) == Map::BLOCK)
        {
          cost += 5;  // extra cost for going through blocks
        }
        const int heuristic = abs(x - targetTileX) + abs(y - targetTileY);
        const coord location(x, y);

        // is this location already on closed list?
        map<coord, Node *>::iterator closedIt = closed.find(location);
        if (closedIt != closed.end())
        {
          // is this child a better path?
          if ((cost + heuristic) < (closedIt->second->cost + closedIt->second->heuristic))
          {
            // update with this better node
            closedIt->second = maker.make(x, y, heuristic, cost, current);
          }

          // done with this child node
          continue;
        }

        // is this location already on open list?
        OpenKey key(cost + heuristic, 0);
        bool onOpenList = false;
        map<OpenKey, Node *>::iterator openIt;
        for (openIt = open.begin(); openIt != open.end(); ++openIt)
        {
          Node * onOpen = openIt->second;

          if (onOpen->location == location)
          {
            onOpenList = true;
            // better path?
            if ((cost + heuristic) < (onOpen->cost + onOpen->heuristic))
            {
              // replace it with this better node
              open.erase(openIt);
              onOpenList = false;
            }
            break;
          }
        } // end for to find same location

        if (onOpenList)
        {
          // already queued by a path at least as good, nothing to add
          continue;
        }

        // add to open map, taking the next tie-break number if needed so that
        // different nodes get different keys
        Node * child = maker.make(x, y, heuristic, cost, current);
        while (!open.insert(pair<OpenKey, Node *>(key, child)).second)
        {
          ++key.second;
        }

      } // end for 0..3

      // add current to closed
      closed.insert(pair<coord, Node *>(current->location, current));


    } // end if not current goal node
  } // end while open list !empty

  return result;
}

bool PlayerCharacterEntity::safeLocationToBomb(int tileX, int tileY)
{
  AIGameView * view = AIGameView::getInstance();

  // set to true, test for disagreements with this
  bool result = true;

  for (int i = 0; i < 4; ++i)
  {
    int x, y;
    x = tileX;
    y = tileY;
    switch (i)
    {
      case 0:
        x++;
        break;
      case 1:
        x--;
        break;
      case 2:
        y++;
        break;
      case 3:
        y--;
        break;
    }

    vector<EntityInfo> info = view->getEntityInfoAtLocation(x, y);
    vector<EntityInfo>::iterator it;

    for (it = info.begin(); it != info.end(); ++it)
    {
      if ((it->groupType == EGG_GROUP && isTeammatePlayer(it->playerNumber))
          || it->type == ENERGY_DROP || it->groupType == POWERUP_GROUP)
      {
        result = false;
        break;
      }
    }

    if (result == false)
    {
      break;
    }

  }

  return result;
}

void PlayerCharacterEntity::checkEnvironment()
{
  // what is the closest enemy in a straight line? might want to launch a rocket
  EntityInfo nearest;
  nearest.id = -1; // marks no suitable enemy found (sorry for the "-1 bashing", free the first non-positive!)

  // modified from RocketSpinEntity::update() code
  bool dirFinished[4] = { false, false, false, false };
  int distance = 0;
  int targetX = 0, targetY = 0;

  while ((!dirFinished[0]) || (!dirFinished[1])
         || (!dirFinished[2]) || (!dirFinished[3]))
  {
    // try each direction in turn once
    // so that rocket targets nearest enemy
    distance++;
    for (int d = 0; d < 4; d++)
    {
      if (dirFinished[d])
      {
        continue;
      }

      DIRECTION dir;
      int xMultiplier, yMultiplier;
      // set these variables
      switch (d)
      {
        case 0:
          dir = UP;
          xMultiplier = 0;
          yMultiplier = -1;
          break;
        case 1:
          dir = DOWN;
          xMultiplier = 0;
          yMultiplier = 1;
          break;
        case 2:
          dir = LEFT;
          xMultiplier = -1;
          yMultiplier = 0;
          break;
        case 3:
          dir = RIGHT;
          xMultiplier = 1;
          yMultiplier = 0;
          break;
      }
      //unused. avoid compiler warning
      (void)dir;
      // get x and y co-ords to test
      int xTile, yTile;
      xTile = m_tileX + (distance * xMultiplier);
      yTile = m_tileY + (distance * yMultiplier);

      if (Map::getInstance()->staticTileAt(xTile, yTile) == Map::EMPTY)
      {
        // test this tile for enemies to launch at
        vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(xTile, yTile);
        vector<EntityInfo>::iterator infoIt;
        for (infoIt = info.begin(); infoIt != info.end(); ++infoIt)
        {
          if (!isTeammatePlayer(infoIt->playerNumber) && (infoIt->groupType == CHARACTER_GROUP
                                         || infoIt->groupType == PLAYER_SPAWN_GROUP
                                         || infoIt->groupType == ROBOT_GROUP) )
          {
            nearest = *infoIt;
            targetX = xTile;
            targetY = yTile;
            break;
          }


        }
        if (nearest.id != -1)
        {
          break;
        }
      }
      else
      {
        // reached a wall or block tile in this direction
        dirFinished[d] = true;
      }


    } // end for each direction

    if (nearest.id != -1)
    {
      break;
    }
  } // end while loop

  if (nearest.id != -1)
  {
    // enemy nearby in straight line
    // check distance is enough not to get caught in blast
    // and safe location to bomb
    if (distance > 1 && safeLocationToBomb(targetX, targetY) && m_health > 10)
    {
      // fire a rocket now! (if possible)
      AIInstruction * fireRocket = new AIInstruction(
        AIInstruction::CREATE_ENTITY, 1, ROCKET_SPIN, getTileX(), getTileY());
      m_insList.push_front(fireRocket);
      return;
    }

  }


  //  are there enemy eggs nearby?
  const int EGG_RANGE = 2;
  if (m_health > 24)
  {
    for (int x = m_tileX - EGG_RANGE; x <= m_tileX + EGG_RANGE; ++x)
    {
      if (x < 0 || x >= GameSettings::getInstance()->getMapWidth())
      {
        continue;
      }
      for (int y = m_tileY - EGG_RANGE; y <= m_tileY + EGG_RANGE; ++y)
      {
        if (y < 0 || y >= GameSettings::getInstance()->getMapHeight())
        {
          continue;
        }

        if (x == m_tileX && y == m_tileY)
        {
          continue;
        }

        vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(x, y);
        vector<EntityInfo>::iterator infoIt;
        for (infoIt = info.begin(); infoIt != info.end(); ++infoIt)
        {
          if (!isTeammatePlayer(infoIt->playerNumber) && infoIt->groupType == EGG_GROUP
              && infoIt->type != EGG1 )
          {
            if (nearest.id == -1)
            {
              // set as found nearest egg entity
              nearest = *infoIt;
              targetX = x;
              targetY = y;
            }
            else
            {
              // new nearest?
              int oldDistance, newDistance;
              oldDistance = abs(targetX - m_tileX) + abs(targetY - m_tileY);
              newDistance = abs(x - m_tileX) + abs(y - m_tileY);
              if (newDistance < oldDistance)
              {
                // replace old found entity
                nearest = *infoIt;
                targetX = x;
                targetY = y;
              }
            }
            break;
          }


        }


      }
    }
  }

  if (nearest.id != -1)
  {
    // we've found a nearby enemy egg

    // check for nearby friendly egg thieves
    bool nearbyThief = false;
    for (int x = targetX - EGG_RANGE; x <= targetX + EGG_RANGE; ++x)
    {
      if (x < 0 || x >= GameSettings::getInstance()->getMapWidth())
      {
        continue;
      }
      for (int y = targetY - EGG_RANGE; y <= targetY + EGG_RANGE; ++y)
      {
        if (y < 0 || y >= GameSettings::getInstance()->getMapHeight())
        {
          continue;
        }

        if (x == m_tileX && y == m_tileY)
        {
          continue;
        }

        vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(x, y);
        vector<EntityInfo>::iterator infoIt;
        for (infoIt = info.begin(); infoIt != info.end(); ++infoIt)
        {
          if (isTeammatePlayer(infoIt->playerNumber) && infoIt->type == EGG_THIEF)
          {
            nearbyThief = true;
            break;
          }
        }

        if (nearbyThief)
        {
          break;
        }
      }
      if (nearbyThief)
      {
        break;
      }
    }


    if ( safeLocationToBomb(targetX, targetY) && !nearbyThief )
    {
      // check possible bombing locations
      coord locations[4];

      for (int i = 0; i < 4; ++i)
      {
        int x, y;
        x = targetX;
        y = targetY;
        switch (i)
        {
          case 0:
            x++;
            break;
          case 1:
            x--;
            break;
          case 2:
            y++;
            break;
          case 3:
            y--;
            break;
        }

        if ( Map::getInstance()->staticTileAt(x, y) == Map::EMPTY
             && safeLocationToBomb(x, y) )
        {
          locations[i].first = x;
          locations[i].second = y;
        }
        else
        {
          locations[i].first = -1;
          locations[i].second = -1;
        }

      } // end for 0..3

      // which location is nearest?
      int nearestLocation = -1;
      for (int i = 0; i <= 3; ++i)
      {
        if (locations[i].first != -1 &&
            safeLocationToBomb(locations[i].first, locations[i].second) )
        {
          // this is a valid location
          if (nearestLocation == -1)
          {
            nearestLocation = i;
          }
          else
          {
            int oldDistance, newDistance;
            oldDistance = abs(locations[nearestLocation].first - m_tileX)
                          + abs(locations[nearestLocation].second - m_tileY);
            newDistance = abs(locations[i].first - m_tileX)
                          + abs(locations[i].second - m_tileY);

            if (newDistance < oldDistance)
            {
              nearestLocation = i;
            }
          }
        }
      }

      if (nearestLocation != -1)
      {
        // we've got a target and a position! :D
        clearGoalsAndInstructions();

        AIInstruction * goTo, * placeBomb, * runAway, * pause;

        goTo = new AIInstruction(
          AIInstruction::GO_TO, 0, getType(),
          locations[nearestLocation].first,
          locations[nearestLocation].second);

        placeBomb = new AIInstruction(
          AIInstruction::CREATE_ENTITY, 1, BOMB,
          locations[nearestLocation].first,
          locations[nearestLocation].second);

        runAway = new AIInstruction(
          AIInstruction::MOVE_FROM_LOCATION, 3,
          getType(), targetX, targetY);

        pause = new AIInstruction(
          AIInstruction::WAIT, 10, getType(), 0, 0);

        m_insList.push_front(pause);
        m_insList.push_front(runAway);
        m_insList.push_front(placeBomb);
        m_insList.push_front(goTo);

        m_checkTimer = 97;

        return;
      }
    }

  }


  // if we're pretty alone, maybe worth placing a mine




}

bool PlayerCharacterEntity::applyControl(bool toAI)
{
  if (m_controlledByAI == toAI)
  {
    return false;
  }
  m_controlledByAI = toAI;
  clearGoalsAndInstructions();
  return true;
}

void PlayerCharacterEntity::clearGoalsAndInstructions()
{
  while (!m_goalList.empty())
  {
    delete m_goalList.front();
    m_goalList.pop_front();
  }
  while (!m_insList.empty())
  {
    delete m_insList.front();
    m_insList.pop_front();
  }
}

void PlayerCharacterEntity::hashExtraState(StateHasher& h) const
{
  h.flag(alive, "player.alive");
  h.flag(m_controlledByAI, "player.controlledByAI");
  h.i32(m_IQ, "player.iq");
  h.i32(m_numberBombsPlaced, "player.bombsPlaced");
  h.i32(m_numberEggsPlaced, "player.eggsPlaced");
  h.i32(m_checkTimer, "player.checkTimer");
  h.i32(m_aiFrame, "player.aiFrame");

  // The bot's plan. Entity ids inside it are written as walk positions (see
  // StateHash.h), so the hash does not depend on the id counter.
  h.count(m_goalList.size(), "player.goals.count");
  for (list<AIGoal *>::const_iterator it = m_goalList.begin(); it != m_goalList.end(); ++it)
  {
    const AIGoal * g = *it;
    h.i32(g->m_goalType, "goal.type");
    h.i32(g->m_targetAmount, "goal.targetAmount");
    h.i32(g->m_tileX, "goal.tileX");
    h.i32(g->m_tileY, "goal.tileY");
    h.i32(g->m_targetType, "goal.targetType");
    h.ref(g->m_targetID, "goal.target");
  }
  h.count(m_insList.size(), "player.instructions.count");
  for (list<AIInstruction *>::const_iterator it = m_insList.begin(); it != m_insList.end(); ++it)
  {
    const AIInstruction * ins = *it;
    h.i32(ins->m_type, "instruction.type");
    h.i32(ins->m_targetAmount, "instruction.targetAmount");
    h.i32(ins->m_targetType, "instruction.targetType");
    h.i32(ins->m_targetTileX, "instruction.targetTileX");
    h.i32(ins->m_targetTileY, "instruction.targetTileY");
  }
  h.count(m_abandonedPickups.size(), "player.abandonedPickups.count");
  for (map<int, int>::const_iterator it = m_abandonedPickups.begin(); it != m_abandonedPickups.end(); ++it)
  {
    h.ref(it->first, "abandoned.pickup");
    h.i32(it->second, "abandoned.frame");
  }
  h.count(m_pickupAttempts.size(), "player.pickupAttempts.count");
  for (map<int, PickupAttempt>::const_iterator it = m_pickupAttempts.begin(); it != m_pickupAttempts.end(); ++it)
  {
    h.ref(it->first, "attempt.pickup");
    h.i32(it->second.bestBlocks, "attempt.bestBlocks");
    h.i32(it->second.bestSteps, "attempt.bestSteps");
    h.i32(it->second.failures, "attempt.failures");
  }
  // Not hashed: m_hitThisTick (an output of the tick, derived from health, which is
  // hashed), m_animCounter / m_animDelay (animation pacing), m_velocityModifier (a
  // data constant). Which local device drives the player is not on the entity at all.
}
