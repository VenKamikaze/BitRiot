/*
 * Simulation.h
 *
 * One round of Bit Riot's deterministic simulation: world setup (start) and the
 * per-tick step. GameEngine, the test harness and (later) netplay and the
 * headless binary all drive it the same way, so a test round goes through exactly
 * the code a real round does. Rendering, the InfoPanel, sound, the menu and input
 * stay in GameEngine.
 *
 *   Simulation sim;                 // one per process
 *   sim.start(config);              // build the world from a MatchConfig
 *   while (!sim.roundOver())
 *     sim.step(inputs);             // one tick; inputs is a TickInputs (TickInputs.h)
 *   uint64_t h = sim.stateHash();   // after any tick
 *
 * ONE PER PROCESS. The simulation still runs on process-lifetime singletons (Map,
 * AIGameView, EntityMessageQueue, EntityFactory, GameOverTimer, GameSettings,
 * SimRandom::sim()), so two cannot coexist. The constructor throws
 * std::logic_error if another Simulation is alive. start() may be called any
 * number of times on the one object ("play again").
 *
 * WHAT start() DOES, in this order (the order is part of the contract, because
 * the constructors it runs draw random numbers):
 *
 *   1. tears down the previous round (entities, then the spawning pool);
 *   2. applies the MatchConfig into GameSettings (the sim keeps reading
 *      GameSettings; see MatchConfig.h) and gives the Map the config's size
 *      (Map::resize: the walls and pillars are rebuilt if the size changed; since
 *      brief 03b the Map holds no bitmaps, so the round brings its own map and the
 *      caller resizes nothing - the window and HUD follow separately);
 *   3. reseeds SimRandom::sim() from config.seed - before anything that draws;
 *   4. builds the EntityManager, whose constructor resets the singletons that
 *      describe the previous world: the message queue, the GameOverTimer (the
 *      latch), the entity ids (restart at 1) and AIGameView (via DynamicMap);
 *   5. clears the Map's blocks, then checks that all five singletons really are
 *      fresh and throws std::logic_error if one is not (so a reset that someone
 *      removes fails loudly instead of leaking last round into this one);
 *   6. builds the SpawningPool (numPlayers * 100 points; draws), spawns one
 *      PlayerCharacterEntity per slot at its fixed start tile (draws), then
 *      seeds blocks (draws once per eligible tile, whatever the percentage).
 *
 * It does not touch InfoPanel, the renderer, input or the menu.
 *
 * step(inputs) RUNS EXACTLY THIS, once per tick (tick n is the n-th step since start):
 *
 *   1. the tick counter becomes n;
 *   1a. the hit flags of the last step are forgotten (hitThisTick);
 *   1b. the tick's control events, in order (ControlEvent, TickInputs.h): a slot's
 *      player becomes a human or a bot, its AI goals and instructions are cleared
 *      and its per-slot input state is reset - and then its human input: for each
 *      slot in order, that slot's PlayerInput is applied to its player (movement,
 *      item selection, placement), using the per-slot input state the sim keeps. The
 *      rules are the table at the top of TickInputs.h. Bits for a slot whose player
 *      is an AI (as of after the events), dead or missing are ignored;
 *   2. TOP OF THE TICK: end = EntityManager::oneOrZeroPlayersRemain(pool can spawn
 *      more); over = GameOverTimer::advance(n, end). The first tick `end` holds
 *      starts a countdown of GameOverTimer::GAME_OVER_COUNTDOWN_TICKS (60) ticks;
 *      `over` is true on the tick the countdown has run out AND `end` still holds;
 *   3. if `over` (the first time): the round's winner is fixed now, from the state
 *      at the top of this tick: EntityManager::getWinningPlayer;
 *   4. SpawningPool::update();
 *   5. EntityManager::runFrame() (bot/monster AI and movement, then the message
 *      queue, then collisions);
 *   6. if `over`: roundOver() becomes true;
 *   7. the players damaged during the step are recorded: hitThisTick(slot).
 *
 * ROUND-END SEMANTICS. A round is over after the step on which step 2 said `over`
 * - after the whole step, not part-way through it: pool and runFrame still run on
 * that final tick. So every caller sees the same number of stepped ticks and the
 * same final state: with t0 the tick on which the end condition first held,
 * roundOver() turns true when tick() == t0 + 60, and the state hash at that point
 * includes the final tick's pool update and frame. The winner() is the one fixed in
 * step 3 (a player who dies in step 5 of the final tick does not change it); the
 * entities, roster and scoreboard stats are those after the final step. "60 ticks"
 * assumes the end condition keeps holding: if it stops and later holds again the
 * round ends on that later tick with no fresh countdown (as before). Stepping a
 * round that is already over is allowed and keeps it over; nothing the first end
 * recorded changes.
 *
 * INPUT. Nothing outside step() touches a player entity: the local layer (InputSampler,
 * InputHandler) turns devices into a TickInputs once per stepped tick and hands it to
 * step(inputs); a replay or a remote peer hands it the same bytes. step() with no
 * argument is step(TickInputs()): no human pressed anything (all-AI rounds, tests).
 * The per-slot input state (is an action held, and which item is selected) lives
 * here, in the sim, and is part of the state hash.
 *
 * ---------------------------------------------------------------------------
 * STATE HASH SPECIFICATION (version 2)
 *
 * stateHash() is the desync detector: two peers whose simulations have stepped
 * the same number of ticks from the same MatchConfig must return the same value.
 * It is FNV-1a 64 over the byte stream below, built with StateHasher (StateHash.h,
 * which defines the algorithm, the byte widths and the "ref" encoding). All values
 * are integers or Q16.16 `fixed_t` raw bits; nothing goes through float or double.
 * "y then x" means: for y = 0..height-1, for x = 0..width-1.
 *
 *   0. i32  STATE_HASH_VERSION (2)
 *   1. u64  tick;  u8 roundOver
 *   2. the settings the sim reads, as GameSettings holds them:
 *        i32 mapWidth, i32 mapHeight, i32 numPlayers,
 *        for slot 1..numPlayers: i32 teamNumberForPlayer(slot),
 *        u8 npcSpawnerEnabled
 *   3. the sim RNG: u64 state, u64 inc
 *   4. the GameOverTimer: u8 triggered, u64 tick it was triggered on
 *   5. the spawning pool: i32 pointsLeft, IQsLeft, ENsLeft, minDelay, maxDelay,
 *        delayCounter
 *   6. the pending message queue: count, then front to back for each message:
 *        i32 type, ref(sender), i32 typeToCreate, teamToCreate, toX, toY, fromX,
 *        fromY, flags, offsetX, offsetY, targetX, targetY
 *   6b. the per-slot input state (version 2; brief 04a), for slot 0..numPlayers-1:
 *        u8 action1Down, u8 action2Down, i32 spawnSelection (an EntityType)
 *   7. the static map, y then x: u8 tile (WALL, EMPTY or BLOCK)
 *   8. the entities, count then each in entityList order (ascending id), every
 *      entity as:
 *        i32 type, playerNumber, tileX, tileY, offsetX, offsetY (raw Q16.16),
 *        velocity (raw Q16.16), health, currentAnimState; u8 walkThroughBlocks;
 *        then its type's own state (GameEntity::hashExtraState):
 *          PLAYER_CHARACTER: u8 alive, u8 controlledByAI, i32 IQ, bombsPlaced,
 *            eggsPlaced, checkTimer, aiFrame; the goal list (count; per goal i32
 *            goalType, targetAmount, tileX, tileY, targetType, ref(targetID)); the
 *            instruction list (count; per instruction i32 type, targetAmount,
 *            targetType, targetTileX, targetTileY); the abandoned-pickup map
 *            (count; per entry ref(pickup id), i32 frame given up on); the
 *            pickup-attempt map (count; per entry ref(pickup id), i32 bestBlocks,
 *            bestSteps, failures)
 *          ROBOT1, DRONE, WALL_EATER, EGG_THIEF (SimpleMovingEntity): i32
 *            decayCounter, brain lastMovingState, brain minMoveTimer;
 *            ROBOT1 adds i32 actionCounter, u8 layBlocks, layMines, fireRockets,
 *            layBombs
 *          EGG1..EGG5: i32 hatchling type, hatchCounter, hatchFlags
 *          BOMB, MINE: i32 fuse
 *          ROCKET_FIRED: i32 direction, toX, toY;   ROCKET_SPIN: i32 lifespan
 *          ENPLUS_POWERUP, IQPLUS_POWERUP: i32 decayCounter
 *          BLOCK, EXPLOSION, ENERGY_DROP, MUNCH_EFFECT, DAMAGE_EFFECT: nothing
 *   9. the per-tile entity lists of DynamicMap::entityMap, y then x: count, then
 *        the tile's list in order (insertion order; collisions and the AI's
 *        "first match" depend on it), each as ref(id)
 *  10. the AIGameView's per-tile lists, y then x: count, then in order, each as
 *        ref(id), i32 playerNumber, type, groupType, u8 staticFlag
 *
 * ref(id) is an entity's position in the step-8 walk, or -1 for an id with no live
 * entity: absolute ids are never hashed (StateHash.h).
 *
 * ENUMS AND FLAGS ARE HASHED AS THEIR C++ VALUES, so the numbering is part of the
 * hash. Renumbering, reordering or inserting into any of these changes what a
 * peer sends, and must bump STATE_HASH_VERSION (Simulation.cpp has static_asserts
 * that stop the build when one moves, as a reminder):
 *   EntityType            BLOCK 0, EGG1..EGG5 1-5, ENPLUS_POWERUP 6, IQPLUS_POWERUP 7,
 *                         BOMB 8, MINE 9, ROCKET_SPIN 10, ROCKET_FIRED 11,
 *                         ENERGY_DROP 12, EXPLOSION 13, PLAYER_CHARACTER 14,
 *                         EGG_THIEF 15, WALL_EATER 16, DRONE 17, ROBOT1 18,
 *                         MUNCH_EFFECT 19, DAMAGE_EFFECT 20
 *   EntityGroupType       BLOCK_GROUP 0, EGG_GROUP 1, POWERUP_GROUP 2, WEAPON_GROUP 3,
 *                         EXPLOSION_GROUP 4, CHARACTER_GROUP 5, PLAYER_SPAWN_GROUP 6,
 *                         MONSTER_GROUP 7, ROBOT_GROUP 8, EFFECT_GROUP 9
 *   Map::StaticTile       WALL 0, EMPTY 1, BLOCK 2
 *   DIRECTION             UP 0, DOWN 1, LEFT 2, RIGHT 3 (a rocket's direction)
 *   EntityAnim::ANIM_STATE (the anim state)   IDLE 0, MOVE_UP 1, MOVE_DOWN 2,
 *                         MOVE_LEFT 3, MOVE_RIGHT 4 (SimpleBrain::ANIM_STATE agrees)
 *   EntityMessage::MessageType   DESTROY_ENTITY 0, CREATE_ENTITY 1, MOVE_ENTITY 2
 *   AIGoal::GOAL_TYPES    INCREASE_ENERGY 0, SECURE_AREA 1, CREATE_ENTITIES 2,
 *                         ATTACK_TEAM 3, ATTACK_ENTITY 4, GET_POWERUP 5
 *   AIInstruction::INSTRUCTION_TYPES   GO_TO 0, MOVE_FROM_LOCATION 1,
 *                         CREATE_ENTITY 2, WAIT 3
 *   GameEntity creation flags (message flags, egg hatch flags)   the bit values of
 *                         UP_FLAG (1<<1) .. AI_CONTROLLED_BOT_FLAG (1<<10)
 * Adding, removing or changing the meaning of a HASHED FIELD is the same kind of
 * change: bump the version. Every write names its field, and
 * stateHashFields() lists those names in hash order;
 * SimulationTest.TheHashSchemaIsPinned compares the list with a copy in the test
 * that does not depend on how the simulation plays.
 *
 * NOT HASHED, on purpose:
 *   - m_currentAnimFrame, and the animation pacing counters m_animCounter /
 *     m_animDelay / a mine's m_animDelay: render-only. (A DAMAGE_EFFECT's
 *     m_currentAnimFrame comes from the cosmetic RNG and differs per machine.)
 *   - the cosmetic RNG, absolute entity ids, the entity group and static/solid
 *     flags (all fixed by the type), data-file constants (velocity modifiers,
 *     hatch times, min/max delays) once they are in a hashed counter;
 *   - local-only choices: genders, portraits (which device drives a slot is not in the
 *     simulation at all since brief 04b);
 *   - hitThisTick: an output of the step, derived from health, which is hashed;
 *   - derived or recorded results: the round roster, winner(), the block
 *     percentage after start() (its effect is in the map and entities);
 *   - the sampled input itself (PlayerInput bits): a tick's input is not state, it is
 *     its effect, and the effect (a move, a hold, a placed item) is hashed;
 *   - the device side (which keyboard set or pad drives a slot, the sampler's held and
 *     released edges): local.
 *
 * The message queue (6) and the tile lists (9, 10) are in the hash although the
 * brief did not list the queue: a tick ends with messages still waiting (the
 * collision results), and they decide the next tick.
 * ---------------------------------------------------------------------------
 */

#ifndef _SIMULATION_H
#define _SIMULATION_H

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "EntityEnum.h"
#include "EntityManager.h"
#include "MatchConfig.h"
#include "PlayerCharacterEntity.h"
#include "SpawningPool.h"
#include "TickInputs.h"

class Simulation
{
  public:
    static const int32_t STATE_HASH_VERSION = 2;

    Simulation();
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // Builds a fresh round from the config (see the top of this file for the
    // order of events). Throws std::invalid_argument if the config is not
    // playable, std::runtime_error if Map is not already the config's size and
    // std::logic_error if a singleton was not reset.
    void start(const MatchConfig& config);

    // One tick (see the top of this file). Throws std::logic_error if start() has
    // not been called, and std::out_of_range / std::invalid_argument, before anything
    // of the tick has happened, if `inputs.control` names a slot outside the round's
    // players or an unknown kind.
    void step(const TickInputs& inputs);
    // A tick in which no human pressed anything.
    void step() { step(TickInputs()); }

    // The state hash (specification above). Valid any time after start().
    uint64_t stateHash() const;

    // The names of the fields stateHash() writes, as "name:kind" in hash order,
    // one entry per write (so a repeated item repeats). Values never appear, so
    // it describes the hash's layout, not the round. For the schema test.
    std::vector<std::string> stateHashFields() const;

    // -- read-only state -------------------------------------------------------

    bool started() const { return static_cast<bool>(m_entityManager); }

    // Number of steps since start(). 0 straight after start().
    uint64_t tick() const { return m_tick; }

    // True once a step has completed in which the game-over countdown ran out.
    bool roundOver() const { return m_roundOver; }

    // The tick on which the round ended (tick() at that step), 0 while it is running.
    uint64_t roundEndedAtTick() const { return m_roundEndedAtTick; }

    // The winning player, fixed at the top of the tick the round ended on;
    // nullptr for a draw, and while the round is still running.
    std::shared_ptr<PlayerCharacterEntity> winner() const { return m_winner; }

    // The winner as of right now: what winner() would be if the round ended this
    // instant (nullptr unless the end condition holds). GameEngine uses it for the
    // local ESC-to-scoreboard path.
    std::shared_ptr<PlayerCharacterEntity> currentWinner();

    EntityManager& entityManager() { return *m_entityManager; }
    SpawningPool& spawningPool() { return *m_spawningPool; }

    // Every player character created this round, in creation order, dead ones
    // included (for the scoreboard).
    const std::vector<std::shared_ptr<PlayerCharacterEntity>>& roster()
    {
      return m_entityManager->getRoundPlayerRoster();
    }

    const MatchConfig& config() const { return m_config; }

    // What the sim remembers of a slot's input between ticks: whether an action is
    // held and which item it has selected. SLOT is playerNumber - 1 (0 to
    // MAX_PLAYERS - 1; anything else throws std::out_of_range). Hashed. The
    // InfoPanel is fed from this.
    struct SlotInputState
    {
      bool action1Down = false;
      bool action2Down = false;
      EntityType spawnSelection = BLOCK; // BLOCK whenever neither action is down
    };
    const SlotInputState& inputState(int slot) const;

    // The item slot's held action has selected, or PLAYER_CHARACTER (the "no
    // selection" marker) while neither action is held. This is what the HUD shows:
    // the InfoPanel reads it and nothing writes the HUD from the sim.
    EntityType heldSelection(int slot) const;

    // Whether the player of `slot` (playerNumber - 1) was damaged during the step that
    // has just run: set by that step, cleared at the start of the next (so it is
    // meaningful between two steps) and false after start(). The local layer
    // rumbles the controller of every slot for which it is true; the simulation
    // itself touches no device. Any player can be hit, bots included: whether a slot
    // has a controller is not the simulation's business. Not part of the state hash
    // (it follows from health, which is). Slot out of range: std::out_of_range.
    bool hitThisTick(int slot) const;

    // Number of Simulation objects alive in this process (0 or 1).
    static int instancesAlive();

  private:
    void tearDown();
    void hashInto(StateHasher& h) const;
    void checkSingletonsAreFresh() const;
    void spawnPlayers();
    void seedBlocks();
    void validateControlEvents(const std::vector<ControlEvent>& events) const;
    void applyControlEvents(const std::vector<ControlEvent>& events);
    void applyInputs(const TickInputs& inputs);
    void applySlotInput(int slot, PlayerInput input);
    std::shared_ptr<PlayerCharacterEntity> playerForSlot(int slot) const;
    void clearHits();
    void collectHits();

    std::unique_ptr<EntityManager> m_entityManager;
    std::unique_ptr<SpawningPool> m_spawningPool;
    MatchConfig m_config;
    uint64_t m_tick = 0;
    bool m_roundOver = false;
    uint64_t m_roundEndedAtTick = 0;
    std::shared_ptr<PlayerCharacterEntity> m_winner;
    std::array<SlotInputState, std::GameSettings::MAX_PLAYERS> m_slotInput;
    std::array<bool, std::GameSettings::MAX_PLAYERS> m_hitThisTick{};
};

#endif /* _SIMULATION_H */
