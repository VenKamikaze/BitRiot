# 03a — `Simulation`, `MatchConfig` and state hash

- **Repo:** BitRiot
- **Branch:** `netplay/03a-simulation`
- **Depends on:** 01, 02, 02b
- **Status:** done (play-test pending)

## Goal

Move round setup and the per-tick step out of `GameEngine` into a `Simulation` class. The game, the test harness, and later netplay and a headless binary all drive it the same way. Add a deterministic, fully specified state hash.

When this lands, a harness round goes through exactly the code path a real round does.

Decoupling the simulation from bitmaps and SDL surfaces, and the frame loop, are brief 03b.

## Read first

- [`research/code-facts.md`](research/code-facts.md): §1 (`runEngine` order), §5 (iteration order and hazards), §7 (shared settings), §10 (harness gaps), and §12 (anim state, `GameSettings` reads, the round-end tick).
- `src/GameEngine.cpp`: `runEngine`, `resetGame`, `seedBlocksOnMap`, `initPlayerCharacters`, `applyMapDimensionsIfChanged`.
- `tests/support/HeadlessGame.{h,cpp}`. The harness currently duplicates spawn positions and seeds no blocks.
- `src/EntityManager.*`, `src/SpawningPool.*`, `src/InfoPanel.*`.

## In scope

1. **`MatchConfig`.** A plain struct holding everything that must be identical on every peer:
   - the seed (from 02)
   - map width and height
   - block percentage
   - number of players
   - each slot's team and initial controller (human or AI)
   - `npcSpawnerEnabled`, frozen for the match
   - any other setting the sim reads

   The sim reads `GameSettings` everywhere: teams on every collision, map dimensions, and so on (code-facts §12). Converting every read would be a large change. Instead, **`Simulation::start` applies `MatchConfig` into `GameSettings`**, and the sim keeps reading `GameSettings`. Document this. Netplay (07) will snapshot and restore the local menu settings around a networked match, so expose a way to do that, or at least leave a note.
2. **The `Simulation` class.**
   - `start(const MatchConfig&)` performs today's `resetGame()` world setup: reseed (02), blocks, players and pool. It does not touch `InfoPanel`, the renderer or the menu.
   - **`start` must reset every sim singleton or assert that it is fresh:** `Map` blocks, `AIGameView`, `EntityMessageQueue`, `EntityFactory` ids and `GameOverTimer`.
   - `step()` runs exactly today's per-tick order. **Pin down the round-end semantics** (code-facts §12):
     1. At the top of the tick: the win check and GameOverTimer evaluation.
     2. Then the full step: pool update → `runFrame`.
     3. `roundOver()` becomes true only after that full step.

     Every caller sees the same number of stepped ticks and the same final state. Document this precisely, because netplay compares final hashes.
   - Human input is still applied by `GameEngine` before `step()` for now. Brief 04a changes this to `step(inputs)`, so leave a clear seam for it.
   - Read-only accessors: tick number, round-over flag and result, and the entity manager/roster.
   - **Only one `Simulation` per process.** The singletons make this unavoidable. Assert it.
3. **`GameEngine` uses `Simulation`.** Rendering, InfoPanel, sound, the menu and input stay in `GameEngine`. Behaviour for players must be unchanged.
4. **The harness uses `Simulation`.**
   - Delete the duplicated setup code.
   - To keep this cycle reviewable, **the harness defaults to `blockPercentage = 0`**, so existing tests keep their open-ground scenarios. Real block seeding is covered by new tests (below).
   - Change test setups only where needed, never assertions. List every changed test.
5. **`uint64_t Simulation::stateHash()`.** Use FNV-1a 64, or another algorithm you fully specify, over an order-stable walk that **includes**:
   - the tick number
   - each entity, in `entityList` order:
     - type
     - tile
     - `m_offsetX/Y` and `m_velocity` as raw bit patterns
     - health
     - player number
     - alive flag
     - `m_controlledByAI`
     - **`m_currentAnimState`** (sim state: it drives movement)
     - per-type counters (fuses, timers, AI wait and check timers, goal/instruction state as far as it can be cheaply reached)
   - **per-tile list order** of `entityMap[x][y]` and the AIGameView tile lists. Collisions and AI "first match" depend on it.
   - static map tiles
   - spawning pool state
   - GameOverTimer state
   - sim RNG state

   It **excludes** `m_currentAnimFrame`, `m_animCounter`, the cosmetic RNG and absolute entity ids (use position in the walk instead).

   Document exactly what goes in, as the desync detector everything later relies on. Brief 04a adds the per-slot input state to it.
6. **Integers only.** After 02b the sim has no floats (D6). The hash covers only integer and fixed-point fields, and must not convert anything through `float` or `double`.

## Out of scope

- Decoupling from bitmaps and surfaces, the sim library target, and the frame loop (03b).
- The input refactor (04a).
- Replay (05).
- Removing the singletons.

## Tests

- The existing suite passes through `Simulation`, with setup changes listed.
- **Real rounds:** with the default block percentage on a real-sized map, the round has blocks consistent with the config and plays to a natural end.
- **Hash stability:** the same seed and config give an identical hash on every tick across two runs.
- **Singleton reset:** run round A, then a different round B, then A again. The repeat reproduces A's per-tick hashes. This test must fail if any singleton reset is removed.
- **Hash sensitivity:** changing one entity's health, position or anim state, or advancing the RNG, changes the hash.
- **Round-end semantics:** a test asserting the documented tick count at the end of a round.
- Run a 200-seed sweep before and after, and explain any change.

## Acceptance criteria

- `GameEngine` delegates world setup and stepping to `Simulation`.
- The harness has no duplicated setup.
- `stateHash()` is specified, documented and tested.
- The singleton-reset test passes.
- Gameplay is unchanged for players. This needs a play-test, which may be pending at merge (see README).

## Outcome

**What landed**

1. **`MatchConfig`** (`src/MatchConfig.{h,cpp}`): seed, map width and height, block percentage, `npcSpawnerEnabled`, and one `Slot{team, ai}` per player (the player count is `slots.size()`). `fromSettings(GameSettings&)` copies the match-relevant settings (not the seed), `applyTo(GameSettings&)` writes a config back. There are two validity checks: `runnableProblem()` is the loose one `start()` applies (2-6 players, no negative team, map at least 4x4, so tests can use small maps), and `problem()` is the strict one for a config that arrives from another machine (every team below the player count, an odd map inside `GameSettings`' width and height limits and tall enough for the player count, block percentage inside the menu's range, plus everything `runnableProblem()` checks); netplay must call `problem()` before `start()`. The sim keeps reading `GameSettings`; **`Simulation::start()` applies the config into it** (documented at the top of `MatchConfig.h`). Genders, portraits and devices are not in the config and `start()` never reads them, **but applying a config with fewer players than the local roster truncates the roster**, so settings isolation for netplay is `GameSettings::snapshot()` before the match and `restore()` after (item 6; tested 4 players -> host's 2 -> restore).
2. **`Simulation`** (`src/Simulation.{h,cpp}`):
   - `start(config)` does the old `resetGame()` world setup in this order: **validate** (`runnableProblem()`, and `Map` already has the config's size) before anything is touched, so a refused config leaves the running round and `GameSettings` alone; tear down the previous round; apply the config; reseed `SimRandom::sim()` from `config.seed`; build the `EntityManager` (its constructor resets the message queue, `GameOverTimer`, entity ids and `AIGameView`, as in brief 01: that stayed where it was, because tests build an `EntityManager` directly); clear the `Map`'s blocks; **check that all five singletons are fresh and throw `std::logic_error` if not**; then the `SpawningPool`, one player per slot at its fixed start tile, and block seeding. It never touches `InfoPanel`, the renderer, input or the menu.
   - `step()` runs the tick (round-end semantics below). The input seam is a comment: `GameEngine` applies human input before it calls `step()`; 04a makes it `step(inputs)`.
   - Accessors: `tick()`, `roundOver()`, `roundEndedAtTick()`, `winner()`, `currentWinner()`, `entityManager()`, `spawningPool()`, `roster()`, `config()`, `started()`. **One per process:** the constructor throws `std::logic_error` if another is alive; `start()` can be called any number of times ("play again").
3. **`GameEngine` uses it.** `resetGame()` builds the config (`MatchConfig::fromSettings` plus `GameSettings::pickRoundSeed()`), calls `m_sim.start()`, then does the presentation side (InfoPanel, input reset, each player's controller and team sprite, `InputHandler::setPointers`). `GAME_RUNNING` is: input, `m_sim.step()`, scoreboard if `roundOver()` (or ESC), render. `m_pEntityManager`, `m_pSpawningPool`, `m_gameTicks`, `seedBlocksOnMap` and the world-spawning half of `initPlayerCharacters` are gone from the engine.
4. **The harness uses it.** `startRound()` builds a `MatchConfig` from `GameSettings` and calls `Simulation::start()`. The copy of the start tiles is gone. The harness defaults to `blockPercentage = 0` (`setBlockPercentage()` changes it). `sim()` returns the test's `Simulation`; `em()` and `pool()` are its parts.
5. **`stateHash()`**: FNV-1a 64 over a byte stream of integers. Specified in full in the comment at the top of `src/Simulation.h` (version 1, `STATE_HASH_VERSION`), with the algorithm and byte widths in `src/StateHash.h`. Every write names its field, and `stateHashFields()` lists the names in hash order (a trace of field names and widths, never values), which the schema test pins. The enums the hash writes by value are listed in `Simulation.h` and guarded by `static_assert`s in `Simulation.cpp`: renumbering one fails the build and means bumping `STATE_HASH_VERSION`. Summary below.
6. **Smaller changes.** `GameTimer` has no clock of its own: `advance(tick, endConditionHolds)` and `getTimerCompleted(tick)` are given `Simulation::tick()`, which merges brief 01's two per-round tick counters (`GameEngine::m_gameTicks` and `GameTimer::currentTick`) into one. `GameSettings::pickRoundSeed()` (pinned-or-fresh, no reseed) is what the engine and harness use. `beginRoundSeed()` is gone from production code: the seed plumbing tests use a test-support helper (`bitriot_test::beginRoundSeed(settings)`: `pickRoundSeed()` plus a reseed). `GameSettings::snapshot()`/`restore()` save and put back the whole local roster (every `Player` field) and the settings a match overwrites; `restore()` re-derives the genders/AI mirrors. `EntityMessageQueue` holds a `std::deque` (it can be walked; behaviour unchanged). `EntityFactory::nextId()`, `SimRandom::stateValue()/incValue()` and a `hashState` on every entity type with state of its own.

**Round-end semantics** (tested exactly in `SimulationTest.TheRoundEndsASixtyTickCountdownAfterTheConditionFirstHolds` and `TheFinalTickRunsInFullAndTheWinnerIsFixedAtTheTopOfIt`)

Tick *n* is the *n*-th `step()` since `start()`. `step()` does: (1) `tick = n`; (2) at the top, `end = oneOrZeroPlayersRemain(pool.canSpawnMore())` and `over = GameOverTimer::advance(n, end)`; (3) if `over`, the winner is fixed now from the state at the top of the tick; (4) `pool.update()`; (5) `runFrame()`; (6) if `over`, `roundOver()` becomes true. So:

- With *t0* the first tick on which the end condition holds, the round is over **after the whole of tick *t0* + 60** (`GAME_OVER_COUNTDOWN_TICKS`). Pool and `runFrame` run on that last tick, so every caller sees the same number of stepped ticks and the same final state; `roundEndedAtTick()` is that tick.
- `winner()` is the winner at the top of the final tick; a player who dies in that tick's `runFrame` does not change it. The roster, entities and scoreboard stats are those after the step. **Consequence:** the scoreboard's "Team N wins" comes from the fixed winner, but each row's `survived` is read from `isAlive()` after the step, so a winner who dies on that final tick shows "Team N wins" with nobody surviving. It is a one-tick window and was a similar one before (the old code read both before the final `runFrame`). (The engine used to flip to the menu and load the scoreboard before the pool and `runFrame` of that tick; the scoreboard now sees the state one step later. The only visible difference could be a stat or alive flag changing on the very last tick.)
- "60 ticks" assumes the end condition keeps holding; if it stops and holds again the round ends on the later tick with no new countdown (unchanged from brief 01).
- Stepping a finished round is allowed: it stays over and keeps its result.
- **ESC** is still local. The engine applies input first, so a quit is seen in the same call: it still calls `step()` (the rest of the tick runs, as before), then loads the scoreboard with the winner as of the moment ESC was seen (`currentWinner()`).
- The engine now applies input before the win check (it used to be win check, then input). That cannot change the result: input moves players and posts messages, and the win check reads alive flags and entity groups.

**What the hash covers and excludes** (the full list, with field order, is in `Simulation.h`)

- Covers, in this order: version; tick and `roundOver`; the settings the sim reads (map size, player count, each slot's team number, `npcSpawnerEnabled`); the sim RNG state; the `GameOverTimer` (latched, tick latched on); the spawning pool's counters; **the pending message queue**; the static map, y then x; every entity in `entityList` order with type, owner, tile, `m_offsetX/Y` and `m_velocity` as raw Q16.16 bits, health, `m_currentAnimState`, walk-through-blocks and then its type's own counters (player: alive, `m_controlledByAI`, IQ, bombs and eggs placed, `m_checkTimer`, `m_aiFrame`, goal list, instruction list, abandoned-pickup and pickup-attempt maps; robot, drone, wall eater, egg thief: decay counter, brain timers, robot action counter and flags; eggs: hatchling, hatch counter, hatch flags; bomb and mine fuses; rocket direction and target, spinner lifespan; EN+ and IQ+ decay); the `entityMap` per-tile lists in order; the `AIGameView` per-tile lists in order.
- Excludes: `m_currentAnimFrame` (so a `DAMAGE_EFFECT`'s cosmetic frame does not matter), the anim counters and delays, the cosmetic RNG, absolute entity ids, `attachedController`, the group and static/solid flags (fixed by the type), data-file constants, the roster and `winner()` (derived), and the input layer's per-slot state (04a).
- **Entity ids are never hashed.** Anything that refers to another entity (a bot goal's target, a tile list member, a queued message's sender, the pickup maps' keys) is written as `ref(id)`: the entity's position in the walk, or -1 if it no longer exists. The hash is therefore the same whatever the id counter started at.
- **Added to the brief's list:** the pending message queue. A tick ends with collision results still queued (`runFrame` posts them after draining), and they decide the next tick, so two worlds that differ only there are different states. Also the settings block (teams, spawner, map size, player count), so a peer with a different config disagrees at tick 0.
- Only integers and `fixed_t` bits go in; the stream is little-endian byte by byte, so it does not depend on the host.

**Tests** (`tests/SimulationTest.cpp`, 60 new; suite 186 to 246)

- `MatchConfig`: capture and restore, applying into `GameSettings` (every field, including the map size written over a different one), the loose and the strict validation (teams, even, out-of-range and too-short maps, block percentage, the loose limits again), a refused config leaves the running round and settings untouched, a forgotten Map resize is loud.
- **Local settings (`ALocalRosterSurvivesAShorterNetworkedMatch`):** four local players with genders, a controller, a keyboard set, teams and device labels; a host's 2-player match through `start()`; applying a 4-player config afterwards gives default players (shown), and `GameSettings::restore()` of the snapshot brings back every field of all four, the mirrors, block percentage, spawner and map size.
- `Simulation`: one per process, the slot is free again, `step()`/`stateHash()` before `start()` throw, start tiles, `tick() == 0` after `start()`.
- **Real rounds, started like every other test so `BITRIOT_TEST_SEED` sweeps vary them:** blocks match the config (100% fills exactly the eligible tiles, 0% none, 55% lands within five standard deviations of a binomial around it, never on a start corner or pillar, each block entity matches its map tile, on 17x13, 25x19, 13x11; the 55% density is checked on the three maps pooled, which is enough tiles to tell 55% from 40% or 70%, where one small map is not); a 2-bot and a 4-bot round on 17x13 at 55% play to their natural end (block team by corner and the hash tests keep pinned seeds on purpose).
- **Round end:** the exact tick counts above; the timer's latch tick is the sim's tick; a stepped-past round stays over; a harness round's `ticks == frames + 1 + 60`.
- **Hash stability:** the same config gives an identical hash on every one of 900 ticks across two runs; different seeds differ; each config field is in the hash; cosmetic RNG draws during a round change nothing.
- **Singleton reset (`SingletonResetTest`):** round A, a different round B (21x15, 3 players, stopped mid-countdown so it leaves entities, ids, a latched timer and queued messages behind), then A again; the second A reproduces the first tick for tick, with each singleton's stale state planted in turn (a queued message, a latched timer, advanced ids, a ghost in `AIGameView`, a stray block) and ids checked directly.
- **Hash sensitivity (`StateHashTest`):** health, tile, one least-significant bit of an offset, velocity, owner, walk-through-blocks, anim state, every player counter and flag, the bot's pickup memory, the RNG, a map tile, the timer, a queued message, the order of two entities in a tile list and in an `AIGameView` list, egg hatch counters, fuses, spinner lifespan, EN+/IQ+ decay, effect and explosion health; and not: `m_currentAnimFrame`, anim counters, a `DAMAGE_EFFECT`'s cosmetic frame, the cosmetic RNG, `attachedController`.
- **The hash schema (`TheHashSchemaIsPinned`):** a hand-built world holding one of every entity type, a human player carrying a planted goal, instruction and pickup memory (so nothing comes from the bot AI's constructor), and queued messages (never stepped) gives the list of field names the hash writes, in first-written order, compared with a copy in the test. **Entity fields are listed under their entity type** (`ROBOT1/mover.decayCounter`, `BOMB/entity.health`), so a type that stops calling its base class's hashing, or whose `hashExtraState` stops overriding (a signature typo), shows up; a flat list of names collapses the duplicates from several types and could not. It cannot move because of how the sim plays, only because the hash's layout changed.
- `StateHasher`: the published FNV-1a 64 test vectors, byte order, `ref()` encoding.
- **Golden digests** (`PinnedRoundsKeepTheirHashes`): four pinned rounds played to their natural end (4302, 6552, 1210 and 1147 hashes), a digest of every tick's hash each. They pin the hash *and the way the sim plays*, so any legitimate sim, AI or data change moves them; the commit must say so (tests/README.md). `BITRIOT_HASH_DUMP` prints the values and still compares.

**Changed test setups** (no assertion was changed)

- `RoundHygieneTest`: `GameOverTimerEndsExactlyNTicksAfterTheConditionFirstHolds`, `GameOverTimerIgnoresWallClockTime` and the helper `ticksUntilRoundEnds`. `GameTimer` no longer owns a clock (brief 01's follow-up: one tick), so they keep a local tick counter and pass it to `advance(tick, ...)` / `getTimerCompleted(tick)`. Same assertions, same numbers.
- `RoundHygieneTest.GameOverTimerWaitsTheFullCountdownInEveryRound`: drives `Simulation::start()` and `sim().step()` instead of hand-rolling `timer->advance(...)` and `runFrame()`, so it exercises the engine's actual tick. Same assertions (the timer is un-latched at the start of every round, and the round ends on the n-th step after the condition first holds).
- `SimRoundTest::playRound` (`SimRandomTest.cpp`): steps `sim().step()` and digests `stateHash()` per tick instead of an engine-order loop and the entity count. It now ends at `roundOver()` (the countdown included) and the winner is `sim().winner()`. Its assertions (same seed, same result; different seeds differ; cosmetic draws change nothing) stay, but two had to change meaning, both restored: `SameSeedPlaysTheSameFullRoundTwice` asserted `frames > 10` as a "was the round already over at kickoff?" guard, and `frames` now counts to `roundOver()` (at least 61), so it cannot fail; it asserts `conditionFrames > 10`, the ticks before the end condition first held (the `GameOverTimer`'s latch tick - 1), instead. `DifferentSeedsPlayDifferentRounds` would have asserted on a state-hash digest, which contains the RNG state and so differs between seeds whatever the rounds did; the result carries both a `hashDigest` and an `entityDigest` (the entity count of every tick, as before), the same-seed tests compare both, and the different-seeds test asserts on the entity digest.
- `RoundSeedTest.BeginRoundSeedUsesAPinnedSeedAndSeedsTheSimStream` and `APinLastsForOneRoundOnly`: call `bitriot_test::beginRoundSeed(settings)` (test support) where they called `settings->beginRoundSeed()`, which left production code. Same assertions.
- `StateHasherTest` and the other new tests were adapted to the named-field `StateHasher` API as it changed within this branch.
- The harness changes, which every `HeadlessGameTest` inherits (no test source changed for these):
  - `startRound()` goes through `Simulation::start()`, which seeds blocks. The harness defaults to 0% blocks, so there are none, but block seeding still draws one number per eligible tile, so **every harness round's random sequence is different from before** (same effect as the sweep numbers below).
  - `runRoundToCompletion()` now steps `Simulation::step()` until `roundOver()`, so it includes the 60-tick countdown and the whole final tick. `RoundOutcome::frames` keeps its meaning (ticks before the end condition first held), so `EXPECT_GT(outcome.frames, 0)` still detects "over at kickoff"; `ticks` is new. The state in the outcome (players alive, teams, pool, winner) is the state when the round ended. The `FullRoundTest` cases (PvP, team, 2- and 4-player co-op, roster, two and three consecutive rounds, round lengths) are the ones affected; they pass unchanged.
  - `runFrames()` is still a bare `EntityManager::runFrame()` (no tick, pool or win check): scenario tests want a world that holds still. Documented in the header and tests/README.md.

**What was shown to fail without the change**

New API, so the tests cannot be compiled against the old code. Instead each piece was broken in turn in a scratch worktree, the whole suite rebuilt and run, and the file restored: 98 distinct breakages (the ones that survived were re-run after a fix).

| Broken | Result |
|---|---|
| Each of the five singleton resets removed (message queue clear, `GameOverTimer::reset`, `resetIds`, `AIGameView` clear in `DynamicMap`, `Map::clearBlocks`), with the freshness check **on** | `start()` throws `std::logic_error` and the reset test for that singleton fails (also most of the suite for timer/ids, since every later round throws) |
| The same five with the freshness check **also removed** (the hash has to notice) | the reset test for that singleton fails each time: queue `ALeftOverMessage...`; timer `ALatchedGameOverTimer...`; ids `EntityIdsRestartInTheNextRound` (ids are not hashed, so it is the direct id check that fails); `AIGameView` `AGhostInTheAIView...`; `Map` `AStrayBlockInTheMap...` |
| No reseed from `config.seed`; reseed after the pool and players draw | 14 fail incl. same-config-same-hash, the golden digests and the singleton tests |
| `step()`: tick not counted; pool not updated; `runFrame` not run; final tick returns before pool and `runFrame`; winner taken after `runFrame`; round never over; timer given tick+1; win check ignoring the pool | each fails the round-end tests (`TheRoundEnds...`, `TheFinalTickRunsInFull...`, `TheGameOverTimerRunsOnTheSimulationsTick`, the golden digests); the first and sixth hang forever (the test loops wait for a tick or an end that never comes), the run was killed by timeout; the pool-ignoring win check is caught by the existing `RoundLifecycleTest` and `WinConditionTest` cases |
| `start()`: no `problem()` check, no Map size check, config not applied, round seed not recorded; second `Simulation` allowed; instance count not released | one or more of `AConfigThatCannotBePlayedIsRefused`, `StartRefusesAMapThatWasNotResizedFirst`, `StartAppliesTheConfigIntoGameSettings`, `OnlyOneSimulationMayExistPerProcess` (the last one: 162 tests fail, every test after the first leaks the slot) |
| Blocks: wrong corner team, one corner unprotected, percentage ignored; player 2's start tile; slot AI flag ignored | `BlocksBelongToTheTeamOfTheirCorner`, `RealRoundsSeedBlocksAccordingToTheConfig`, `StartBuildsOnePlayerPerSlot...`, `StartAppliesTheConfig...` |
| `MatchConfig`: teams, AI flag, spawner, block percentage, map width or height not applied; `fromSettings` misreading the AI flag or dropping the spawner; negative team and tiny map accepted | `ConfigCapturesTheMatchSettings...`, `StartAppliesTheConfigIntoGameSettings`, `TheConfigIsPartOfTheHash`, `AConfigThatCannotBePlayedIsRefused` |
| Hash drops any one of 51 pieces (each field or group of fields) | 28 are caught by a targeted `StateHashTest`/`SimulationTest` case; the other 23 only by the golden digests: the tick, the round-over flag, map width and height, player count, the timer, the pool as a whole and its points, the entity type, `m_checkTimer`, `m_aiFrame`, the bot goal and instruction lists, egg hatchling and flags, rocket direction and target, the movers' decay counter and brain, the robot's action counter and flags, message fields. That is what the digests are for: any change to the hashed fields moves them |
| Hash gains `m_currentAnimFrame`, a player's anim counter, the cosmetic RNG state, `attachedController`, absolute ids in the entity walk, or writes `ref` as an absolute id | `RenderOnlyAnimationStateIsNotHashed`, `ADamageEffectsCosmeticFrameIsNotHashed`, `TheCosmeticRngIsNotHashed`, `CosmeticDrawsDuringARoundDoNotChangeItsHashes`, `ALocalControllerAssignmentIsNotHashed`, `EntityReferencesAreWalkPositionsNotIds`; an absolute id added to the entity walk is caught only by the golden digests |

- **Survivors found and fixed:** `MatchConfig::applyTo` without `setMapHeight` (the test had set `GameSettings` to the right size first; it now scrambles it), the hash without a bot's abandoned-pickup map (never populated in the pinned rounds), and without the IQ+ decay counter (the pool's IQ+ powerups are disabled, so no round spawns one). Each has a test now and was re-run: fails without its line.
- **Fix round (review items), 30 further breakages in a fresh worktree:**

| Broken | Result |
|---|---|
| `restore()` ignores the roster / does not re-derive the mirrors / forgets the spawner; `snapshot()` forgets the roster | each fails `ALocalRosterSurvivesAShorterNetworkedMatch` |
| Schema: a hashed field dropped (`m_aiFrame`, the brain's `m_minMoveTimer`, the `entityMap` refs), added, renamed, two fields swapped, or one retyped (i32 -> u8) | each fails `TheHashSchemaIsPinned`. A rename fails **only** that test (the golden digests do not see a name). Dropping a field also fails the golden digests, as before, and adding one too |
| Schema, per entity type (third review): `RobotEntity` stops calling `SimpleMovingEntity::hashExtraState`; `SimpleMovingEntity` stops writing the brain; `RocketFiredEntity::hashExtraState` loses its `const` so it no longer overrides | each fails `TheHashSchemaIsPinned` as well as the golden digests. (The first schema list was per field name, and the Robot case was caught only by the golden digests, because `mover.decayCounter` and `brain.*` also come from drones, wall eaters and egg thieves.) |
| **A change in play, not in the hash:** a bot waits one tick longer at the start; a bomb reads another key's fuse from `data/entity.txt`; the AI constructor builds a second goal | fails only `PinnedRoundsKeepTheirHashes`; `TheHashSchemaIsPinned` does not move. This is the point of the schema list |
| `problem()`: team >= players, even map, width range, height range, portrait height, block percentage range, or the runnable limits dropped | each fails `AConfigFromAnotherMachineIsHeldToTheMenuLimits` (the last one only after one player and a negative team were added; it survived the first version) |
| `start()`: the Map-size check moved after the teardown; the validation moved after the teardown | `ARefusedConfigLeavesTheRunningRoundAndTheSettingsAlone` (and `AConfigThatCannotBePlayedIsRefused`) |
| An enum renumbered (`BOMB`/`MINE` swapped in `EntityEnum.h`) | the build fails: `static assertion failed: EntityType numbering is part of the state hash` |
| Blocks: percentage ignored, none placed, one row short | `RealRoundsSeedBlocksAccordingToTheConfig` (all three); none placed also fails `ARealRoundOnARealMapPlaysToItsNaturalEnd`, `AFourPlayerRoundWithBlocksPlaysToItsNaturalEnd` (their precondition that the round has blocks) |
| No reseed from `config.seed`; `GameOverTimer::reset` and the freshness check removed | the converted `SimRoundTest` cases (`SameSeedPlays...`, `CosmeticDraws...`) and `GameOverTimerWaitsTheFullCountdownInEveryRound` fail, among others |
| A digest edited by hand, run with `BITRIOT_HASH_DUMP=1` | `PinnedRoundsKeepTheirHashes` fails (dump mode used to assert nothing) |
| The kickoff guard (third review): the win condition made true at the first tick | `SimRoundTest.SameSeedPlaysTheSameFullRoundTwice` fails on `conditionFrames > 10` (and 36 other tests). The old `frames > 10` could not fail: with the same mutation the round still ran 61 ticks |
| Different seeds (third review): the seed ignored by the sim (`seed(0)`) but added to the state hash | `DifferentSeedsPlayDifferentRounds` fails on the entity digest (1 distinct digest, not 8). With the old hash-digest assertion the digest check passed (the seed is in the hash) and only the stream-position check failed |
| Block density 35% or 75% instead of 55% | `RealRoundsSeedBlocksAccordingToTheConfig` fails on the pooled bound (a single small map could not tell them apart) |
| The AI constructor builds a second goal (play change) | only `PinnedRoundsKeepTheirHashes` moves; the schema list does not, now that the schema world has no bots |
| A refused `start()` that writes one field of the roster (`setPlayerController(1, -1)`) | `ARefusedConfigLeavesTheRunningRoundAndTheSettingsAlone` fails (it compares the whole snapshot, not three fields) |

- **Cannot fail by construction:** `StateHasherTest.IsFnv1a64OverTheBytes` and the byte-order test (known answers, no earlier version); `HashingIsRepeatableAndNeedsNoTick` (a guard that hashing mutates nothing); the golden digests fail only when something moves, by design; `DifferentSeedsGiveDifferentHashes` (guards against a stuck seed).

**Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, from `build/`)

- Before (`claude`, 186 tests): 1 failing seed, **6**, `AISeekEnergyTest.HealthyBotWithAnEggUnderItselfStillGetsOutToAPickupInTime`, the known bot livelock in `TODO`.
- After (246 tests, final code): **0 failing seeds**; all 200 seeds pass every one of the 246 tests. Seed 6 passes now, and that is the changed random sequence, not a fix: the harness round now draws one number per eligible tile for block seeding, so every seed means different rounds. Evidence that the livelock is still there: the same test swept alone over seeds 1..8000 on the new harness fails exactly once, seed **4652**, with the same message (`bot never got out, stuck at (1,1)`); `BITRIOT_TEST_SEED=4652 ./build/tests/bitriot_tests --gtest_filter='AISeekEnergyTest.HealthyBotWithAnEggUnder*'` reproduces it. `TODO` is updated with the new seed.
- Rounds with blocks are in the sweep now: `ARealRoundOnARealMapPlaysToItsNaturalEnd`, `AFourPlayerRoundWithBlocksPlaysToItsNaturalEnd` and `RealRoundsSeedBlocksAccordingToTheConfig` start with the harness-derived seed and 55% blocks (the first version of these pinned a seed, and the harness default is 0%, so no sweep had ever played a round with blocks).

**GCC and Clang** (GCC 13.3 and Clang 18.1, each as a Debug `-O0` build and a Release `-O2` build, in separate build dirs; the test binary run from the source root)

- The whole suite passes on all four builds (246 tests).
- **The per-tick hashes of the four pinned rounds are identical across all four builds:** 13,211 hashes (`BITRIOT_HASH_DUMP=all`); `cmp` says the dumps are byte for byte the same, they are also byte for byte the same as before the review-fix round (the fixes changed the layout of the code, not the hash), and every build satisfies the golden digests. This is an early taste of 05b: GCC against Clang and `-O0` against `-O2`, on x86-64 only. The aggressive-flag build, aarch64 and wasm are still 05b's.
- Clang builds the library without `-Winconsistent-missing-override`: the first version declared only `hashExtraState` with `override` in classes whose other virtuals have none (539 warnings in a full build, 0 now: it is declared `virtual`, like its neighbours).
- An AddressSanitizer + UBSan build of the whole suite (the recipe in tests/README.md with `,undefined` added) is clean on the final code: 246 pass, no reports.

**Manual check of the real engine** (`GameEngine` cannot run headlessly)

A scratch copy of the tree (not committed) was built with an env-var hook in `runEngine()`: `BITRIOT_AUTOPLAY=<seeds>` starts all-AI rounds (2 players, 13x11, the game's default 55% blocks and spawner on, so the first round also resizes the window and `Map` from 17x13), pinning the listed seeds (0 = fresh), logging the state hash after `start()` and after every `step()`, and logging the scoreboard length when a round ends. Run under `xvfb-run` with `SDL_AUDIODRIVER=dummy`.

- Seeds `4242,4242,0`: rounds 1 and 2 each ended after exactly **1146 ticks**, winner player 2, and **every one of their 1147 hashes (tick 0 to 1146) is identical to the harness's** for the same config (the `two bots, 13x11, 55% blocks` golden round), and to each other. Round 3 (a fresh seed, 8068673213265253977) ended after 2126 ticks. The scoreboard length passed to `loadScoreBoard` was 37 s, 37 s and 70 s, equal to `ticks x 33 / 1000` each time (1146 x 33 / 1000 = 37, 2126 x 33 / 1000 = 70). The process exited 0.
- ESC: a second run pushed an ESC key press after tick 200. The engine saw it on tick 201, still stepped that tick, loaded the scoreboard (6 s = 201 x 33 / 1000, roster of 2, no winner) and went to the menu; the next round (pinned to 4242 again) then ran its full 1146 ticks with hashes **identical to the harness again**, so a quit round leaves nothing behind.
- That is the round-setup, step, round-end, scoreboard, ESC and "play again" paths through the real `GameEngine`, with the seed pin working through `MatchConfig`.

**Deviations from the brief**

- **`SimRandom::sim()` is still a process-lifetime static**, not a member of `Simulation` (the 02 Outcome expected 03a to move it). About ten sites draw from it directly (entity and pool constructors); moving it would mean threading a stream pointer through all of them, which belongs with removing the singletons (out of scope). `Simulation::start()` seeds it from the config and `stateHash()` covers it, which is what netplay needs.
- `start()` does not resize the `Map` (it needs the primary SDL surface until 03b); it requires the caller to have done so and throws if not. The engine's `applyMapDimensionsIfChanged()` and the harness's `setMapSize()` still do it. 03b can fold it into `start()` when `Map::init` stops needing a surface.
- The reset of the queue, timer and ids stays in `EntityManager`'s constructor rather than moving into `start()`: tests construct an `EntityManager` directly and assert the queue clear (`ANewEntityManagerStartsWithAnEmptyMessageQueue`). `start()` verifies them instead, as the brief allowed.
- **The message queue is in the hash** and the hash covers the settings block; neither was in the brief's list (reasons above).
- Entity ids: a bot's pickup maps are keyed by entity id and its goals carry `m_targetID`; all are hashed as walk positions, as the brief asked for ids in general.
- `runRoundToCompletion()` includes the game-over countdown (see changed setups); `RoundOutcome::frames` keeps its old meaning.
- `GameSettings::beginRoundSeed()` is gone from production code (it reseeded the global sim stream and only tests called it); a test-support helper replaces it. The game goes through `MatchConfig`.
- `MatchConfig` has two validity checks (`runnableProblem()` for `start()`, strict `problem()` for configs from another machine) because the test maps (4x4 to 9x9, even sizes, 0% and 100% blocks) are outside the menu limits the strict check enforces.
- Block seeding draws one number per eligible tile whatever the percentage (as before), so 0% rounds consume the same draws as 55% rounds.

**Follow-ups**

- **03b:** `Simulation::start()` should build the `Map` itself once `Map::init` no longer needs a surface (today it only checks the size). The golden digests and `BITRIOT_HASH_DUMP=all` are the check that "03a's hashes must be unaffected": they must pass unchanged through the surface decoupling and the frame loop. `Simulation.h` includes `EntityManager.h`, `PlayerCharacterEntity.h` and `SpawningPool.h`, which pull SDL headers through `GameEntity.h`; that is part of what `bitriot_sim` has to untangle. The engine's `GAME_RUNNING` branch is now input, `step()`, scoreboard check, render, which is the shape `frame()` wraps; the ESC path (`currentWinner()` before the step, scoreboard after) is local and should stay in the engine. 
- **04a:** `step(inputs)` replaces the input call in `GameEngine::runEngine()`; add the per-slot input state (`InputHandler::m_action1Down`, `m_action2Down`, `m_spawnSelection`) to the hash after the pending message queue (bump `STATE_HASH_VERSION` and regenerate the golden digests). Human players only differ from bots in `m_controlledByAI` and an empty AI plan, and both are hashed. `Simulation::step()` is where the inputs go: before step 2 of the tick (the old engine applied input after the win check; moving it earlier changed nothing, see above).
- **04b:** `m_controlledByAI` is hashed (a takeover changes the hash). Rumble is still called from `PlayerCharacterEntity::onCollision`; `attachedController` is deliberately not hashed.
- **05:** a `MatchConfig` is everything a replay needs of the setup; `Simulation::start()` with the same config reproduces tick 0 exactly (`SingletonResetTest` and the golden rounds are the evidence). The seed is in the config and `start()` consumes any pin. `Simulation::dataHash()` (the data files) is still to do. Checkpoints can use `stateHash()` every K ticks; recording `tick()` with each is free.
- **05b:** run `BITRIOT_HASH_DUMP=all` on every target (aggressive flags, aarch64, wasm) and compare with the GCC and Clang dump byte for byte. Build the golden digests into the gate. The rule "at most one sim draw per full-expression" (02) is exercised by the Clang run above but still only checked by reading the code.
- **07:** `MatchConfig` carries only what the sim reads. A client saves its local settings with `GameSettings::snapshot()` before a match and `restore()`s after (not `applyTo`, which would bring a shortened roster back with default players), checks a host's config with the strict `problem()`, and resizes the `Map` and window to the host's dimensions before `start()` and back afterwards. Compare `stateHash()` at agreed ticks: all state that decides the next tick is in it, including the pending message queue.
- **Hash coverage to watch:** the pool's IQ+ powerups are disabled, so no round exercises that entity; the hash covers it (tested directly). Any new per-entity counter must be added to its `hashExtraState()` with a field name, bump `STATE_HASH_VERSION`, go in `HASH_SCHEMA` in `SimulationTest.cpp` (`BITRIOT_HASH_SCHEMA_DUMP=1` prints it) and move the golden digests.
- **Golden digests are a maintenance chore, by design:** any legitimate sim, AI or data change moves them (see tests/README.md for how to regenerate); the schema list is what protects each field when they are regenerated. 04a will move both.
- The `TODO` livelock entry now lists seed 4652.

**Play-test** (pending; needs a display, so it was not run in the cloud container). `resetGame()` and the order inside `GAME_RUNNING` changed, so the user should play it. The commands are untested on Arch (the container is Ubuntu): RmlUi 4.3 is not packaged for Arch, so it is built from source as `scripts/cloud-setup.sh` does, with two flags Arch's newer tools need. RmlUi 4.3 declares `cmake_minimum_required(VERSION 3.1)`, which CMake 4.x refuses unless told a minimum policy version, and GCC 15 no longer pulls in `<cstdint>` transitively, so it is forced in (the same workaround BitRiot's own CMakeLists applies).

```bash
# one-time: dependencies
sudo pacman -S --needed base-devel cmake git sdl2 sdl2_mixer sdl2_ttf sdl2_image freetype2 mesa
git clone --depth 1 --branch 4.3 https://github.com/mikke89/RmlUi.git /tmp/RmlUi
cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_SAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_LUA_BINDINGS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"
cmake --build /tmp/RmlUi/build -j"$(nproc)" && sudo cmake --install /tmp/RmlUi/build && sudo ldconfig

# AFTER: this branch (it is on origin; use `claude` instead once it is merged)
git clone --branch netplay/03a-simulation https://github.com/VenKamikaze/BitRiot.git ~/bitriot-after
cmake -S ~/bitriot-after -B ~/bitriot-after/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-after/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-after/build && ./BitRiot)

# BEFORE (for comparison): the commit before this branch, 646d55b (brief 02b merged)
git clone https://github.com/VenKamikaze/BitRiot.git ~/bitriot-before
git -C ~/bitriot-before checkout 646d55b
cmake -S ~/bitriot-before -B ~/bitriot-before/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-before/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-before/build && ./BitRiot)
```

`./BitRiot -b` plays on the big 25x19 map; `-f` asks for fullscreen. Play with at least one human and several bots, with the spawner on, and compare with the BEFORE build where useful. Nothing the player sees should change. Check:

1. **Rounds start and end normally.** A round starts with every player on their corner or edge tile and blocks around the map; it ends when one team is left. Play at least one PvP round and, if you use it, one co-op round.
2. **The 2 s countdown.** After the last opponent dies there is a short pause (2 s, 60 ticks) before the scoreboard, and the game keeps running during it. The BEFORE build behaves the same.
3. **The scoreboard.** The winner and "Team N wins" are right, each player's row (eggs, bombs, survived) looks right, and the game length is plausible (about 30 frames a second, so a 90 s round says about 90 s). One known quirk, deliberate: the winner is fixed at the top of the last tick, but each player's `survived` is read after it, so a winner who dies on that very tick would show "Team N wins" with nobody surviving. It is a one-tick window and you are unlikely to see it.
4. **ESC mid-round.** It goes straight to the scoreboard (with no winner unless the round was already decided), and the menu works afterwards.
5. **"Play again".** From the scoreboard, twice in a row: a fresh round each time, a new layout, nothing left over from the last one (no stray eggs or blocks, the countdown is the full 2 s again).
6. **A map-size change between rounds.** In Options change the map size (and the number of players), start a round, check the window, panel and walls are right, then play again and change it back. No crash and no broken walls.
7. **AI takeover via the join selector.** Start with a bot in a slot, press START on a controller (or the keyboard join key) to take it over mid-round, including during the 2 s countdown, and check the player responds to the new device.

Record the result here when it comes back.

**Play-test result:** *(pending)*
