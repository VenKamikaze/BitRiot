# 02b — Fixed-point simulation maths

- **Repo:** BitRiot
- **Branch:** `netplay/02b-fixed-point`
- **Depends on:** 01, 02
- **Status:** done (play-test pending)

## Goal

The simulation uses no floating point at all. Positions, velocities and every other sim quantity that is a float today become integer fixed-point values, so per-tick results are bit-identical on any compiler, optimisation level, CPU and wasm by construction, not by compiler-flag discipline.

The user chose this up front (D6, answered via Q2 in Oct 2026), rather than keeping floats and only converting if brief 05b's gate failed.

It lands before 03a, so the state hash only ever covers integers.

## Read first

- [`README.md`](README.md): D6.
- [`research/code-facts.md`](research/code-facts.md):
  - §4: every float in the sim, and the fixed-point conversion scope
  - §12: `m_currentAnimState` is sim state
- The **Outcome** sections of briefs 01 and 02.
- `tests/README.md`.
- Re-find the floats with `grep -rn "float\|double" src`. Don't trust code-facts line numbers.

## Design

**Representation**

- One type for sim fixed-point values, e.g. `using fixed = int32_t;` plus `constexpr int FIXED_SHIFT = 16;` and `FIXED_ONE = 1 << 16`, in a small header such as `src/Fixed.h`. A thin wrapper struct is fine if it stays trivially copyable, but plain `int32_t` with named helpers is enough.
- Offsets keep today's units and range (`-1 < offset < 1`, see `GameEntity.h`), scaled by 2^16. Velocities likewise.
- Every arithmetic helper is defined for negative values. **Write down the rounding rule** for each one in the header. In particular, `>>` on a negative value floors, while `/` truncates toward zero, and those differ.

**Converting the values**

- **Velocities** are `(float)(health + k) / modifier` today, and `health / ROCKET_VELOCITY` for rockets. Compute them in 64-bit as `((health + k) << 16) / modifier`, **rounded to nearest** (state the rule for ties).
  - Example: a player's 110/400 = 0.275 tiles/tick becomes 18022/65536 ≈ 0.27499. A tile still takes the same number of ticks to cross in almost every case. The Outcome reports any velocity whose ticks-per-tile changes.
  - `health + k` and the modifiers come from data files. If any can be ≤ 0, define the result rather than letting it be UB.
- **Movement** (`GameEntity::moveInDirection`): mechanical. It only adds, subtracts and compares. Keep the branch structure identical.
- **`nearTileCenter`** (PlayerCharacterEntity and SimpleBrain) and the rocket's arrival checks: integer abs and compare.
  - **Check which `abs` the rocket code calls today** (`RocketFiredEntity.cpp`, `abs(m_offsetX)` on a float). If it resolves to the `int` overload, today's code truncates, and that behaviour must be preserved or the change called out.
- **`SimpleBrain`** holds `const float &` references to its owner's offsets and velocity. They become references to the fixed type.
- **Entity creation with offsets** (`EntityFactory`, `EntityMessage`, `EntityMessageQueue`, `EffectEntity`, `SimpleMovingEntity`): the offset parameters become fixed.
- **Rocket target tile:** `RocketSpinEntity` passes the destination tile through the float offset parameters into `RocketFiredEntity`. Pass it as integers. Adding explicit parameters, or a small struct, is fine.
- **A\* open list** (`PlayerCharacterEntity`): `map<float, Node*>` with `+0.001f` nudges on key collision becomes an integer key that keeps the same order. For example, `map<pair<int,int>, Node*>` keyed by `(cost + heuristic, insertion sequence)`, so equal totals pop in insertion order as they do today.
  - Show that the search returns the same paths as before on the existing AI tests.
- **Rendering** stays float or int as convenient, but reads the sim through accessors. It must not write back. Convert `offset * (TILE_WIDTH/2)` with the same truncation the `(int)` cast gives today, so sprites don't shift by a pixel.
- **Non-sim floats stay:** rumble strength, `GameSettings::mapRatio`, RmlUi and audio. List every float that remains in `src/` in the Outcome, each with a one-line reason.

**Guard**

- Add a test, or a CMake/ctest check, that fails if `float` or `double` appears in a sim source file. Keep an explicit allowlist of non-sim files.
  - Brief 03b creates the `bitriot_sim` target. Until then, the list of sim files is maintained by hand, and 03b switches the check to that target's sources.
- Keep `-ffp-contract=off` and the configure-time `-ffast-math`/`-Ofast` rejection **out** of scope. The sim no longer depends on them. Mention in the Outcome whether anything float-sensitive remains that would want them.

## Out of scope

- Changing gameplay constants or velocities beyond the rounding above.
- The state hash (03a), the frame loop (03b), and the cross-platform gate itself (05b).
- Reworking movement logic beyond the type change.

## Tests

- **Fixed helpers:** known answers for conversion, rounding (positive, negative, ties) and abs.
- **Velocity table:** for every entity type, the fixed velocity at initial and max health equals a hard-coded expected value. Ticks to cross one tile are reported in the Outcome alongside the float value.
- **Movement equivalence:** for each direction, with and without a blocking tile, starting at assorted offsets, `moveInDirection` produces the expected fixed offsets and tile changes.
- **A\* order:** a search with many equal-cost frontier nodes returns the same path as before the change. Prove it by running the test against the float version first.
- **Rocket target:** a fired rocket still explodes at the destination tile.
- **No-float guard:** fails if a `float` is added to a sim file (prove it by adding one).
- Run the full suite and a 200-seed sweep before and after. Behaviour changes slightly, so report failure counts, not seed-by-seed comparisons.

## Play-test

Movement speed and feel must match today's. Mark the brief `done (play-test pending)` and give the user commands to build and play a few rounds with humans and bots, including rockets and robots.

## Acceptance criteria

- No `float` or `double` in sim code, and the guard enforces it.
- All tests pass, and the sweep is no worse than before.
- The Outcome lists the remaining floats, the velocity table (float vs fixed, ticks per tile), and any behaviour change.
- code-facts §4 and README D6 are updated to describe what landed.

## Outcome

**Status: done, play-test pending.** The sim has no floating point left, and a test keeps it that way. Everything automated passes, on GCC and on Clang. The conversion changes behaviour in two small ways (monster steering ties, and one-pixel / one-tick quantisation of slow movers), listed below; rocket timing is **identical** to the float code after a review round (see item 2). The user should confirm in play that the remaining changes are not noticeable.

**What landed**

1. **`src/Fixed.h`.** `fixed_t` is an `int32_t` in Q16.16 (`FIXED_ONE` = 65536); it is called `fixed_t`, not `fixed`, because `using namespace std;` is in force in several headers and `std::fixed` (an iostream manipulator) makes the bare name ambiguous. Helpers, each with its rounding rule in the header comment:
   - `fixedFromInt(n)`: exact.
   - `fixedFromRatio(n, d)`: nearest; exact ties round **away from zero**; symmetric for negatives (so `-n/d` is `-(n/d)`); `d <= 0` gives 0 (the float code gave a *negative* velocity for a negative modifier, and inf for zero; no data file has either); saturates to the int32 range. The arguments are `int32_t`, so the scaled numerator cannot overflow (an earlier `int64_t` version returned `INT32_MAX` for representable results once the numerator passed 2^48).
   - `fixedFromRatioCeil(n, d)`: rounds **up** (toward +infinity: the smallest value not below the quotient); same `d <= 0` and saturation rules. Used for the rocket only (item 2).
   - `fixedAbs(v)`: `INT32_MIN`, the one value with no positive counterpart, saturates.
   - `fixedToPixels(v, ppu)`: truncates toward zero, which is what the old `(int)(offset * 16)` cast did. (`>>` would floor, which is different for negatives.)
2. **Velocities** are `fixedFromRatio(health + k, modifier)`: player `k=10` with `PLAYER_VELOCITY_MODIFIER`; robot, drone, wall eater, egg thief `k=2` with their modifiers; rocket `10 / ROCKET_VELOCITY`, **rounded up** with `fixedFromRatioCeil` (26215, where nearest gives 26214; see Behaviour changes). Table below.
3. **Movement** (`GameEntity::moveInDirection`): the same branches, with integer adds, subtracts and compares; `0.0f` became `0`, `1.0f` `FIXED_ONE`, `2.0f` `2 * FIXED_ONE`.
4. **`nearTileCenter`** (player and `SimpleBrain`) and the **rocket arrival checks** use `fixedAbs`. **What `abs` resolved to before:** the rocket's `abs(m_offsetX)` called the **float** overload (`using namespace std;` plus `<cmath>`; checked with a `static_assert` on the old code, which accepted `float` and rejected `int`). So today's code did not truncate, nothing needed preserving, and the brief's worry did not apply.
5. **Entity creation** carries fixed offsets (`EntityMessage`, `EntityMessageQueue::postCreateMessage`, `EntityFactory::getEntity`, `EffectEntity`, `SimpleMovingEntity`). The **rocket target tile** now travels as two ints (`targetX`, `targetY`, defaulting to 0 like the old default `0.0f`) through the same chain into `RocketFiredEntity`, and `RocketSpinEntity::fireAt` passes `destX, destY` there; nothing is smuggled through offsets any more.
6. **A\* open list** is `map<pair<int,int>, Node*>` keyed by `(cost + heuristic, tie-break)`, where the tie-break is the next free number counting up from 0.
   - **The brief's suggestion, insertion order, would have changed the routes.** The float key took the next free `+0.001f`, and a key freed when a node left the open list was reused by the next arrival with that total, ahead of nodes that had been waiting longer. `(total, next free number)` reproduces that exactly. A `(total, insertion sequence)` key was tried and gives different routes.
   - **Result:** 2592 searches over 48 block layouts (routes followed hop by hop, with and without bombing through blocks) plus nine open-board cases choose identical routes before and after.
7. **Rendering and the HUD** use `fixedToPixels`, so sprites are drawn on the same pixel as today. A test sweeps every offset from -1 to +1 against the old float expression and they agree on all of them.
8. Removed `GameSettings::mapRatio` (an unused `float`).
9. **Guard.** `FixedPointSourceGuardTest` scans every `src/` file for `float`, `double`, floating-point literals, `<cmath>`/`<math.h>` libm calls (hypot, exp2, log2, cbrt, trunc, fmin/fmax, asin..tanh and the rest), `atof`/`strto[fd]`/`sto[fd]`, `float_t`/`double_t`/`GLfloat`/`SDL_FPoint`, hex-float literals and character literals such as `'"'` (handled so they cannot hide what follows), and fails on any not on an explicit allowlist (whole non-sim files and directories with a reason each, plus one excused line). **What a text scan cannot see:** an `auto` initialised from something that returns a double, and an implicit int to float conversion at a call; see the 03b follow-up. A new file counts as simulation until listed. Brief 03b can point it at the `bitriot_sim` target's sources. Stale allowlist entries fail too.

**Velocity table** (from the real classes; `ticks/tile` is 2 / velocity, `10 tiles` is the ticks the real movement code takes to cross ten tiles from rest). **No velocity's whole number of ticks per tile changes**; the nearest thing is flagged.

| Entity | Health | Formula | Float | Fixed (raw / 65536) | Ticks per tile float / fixed | 10 tiles float / fixed |
|---|---|---|---|---|---|---|
| Player | 100 (initial) | 110/400 | 0.275000006 | 18022 (0.274994) | 7.27273 / 7.27289 | 70 / 70 |
| Player | 200 (max) | 210/400 | 0.524999976 | 34406 (0.524994) | 3.80952 / 3.80957 | 37 / 37 |
| Player | 1 | 11/400 | 0.027500000 | 1802 (0.027496) | 72.7273 / 72.7370 | 691 / **692** |
| Robot | 38 (initial = max) | 40/190 | 0.210526317 | 13797 (0.210526) | 9.50000 / 9.50004 | 91 / 91 |
| Robot | 1 | 3/190 | 0.015789473 | 1035 (0.015793) | 126.667 / 126.640 | 1204 / 1204 |
| Drone | 35 | 37/180 | 0.205555558 | 13471 (0.205551) | 9.72973 / 9.72994 | 93 / 93 |
| Drone | 1 | 3/180 | 0.016666668 | 1092 (0.016663) | 119.9999 / 120.029 | 1141 / 1141 |
| Wall eater | 25 | 27/160 | 0.168750003 | 11059 (0.168747) | 11.8519 / 11.8521 | 113 / 113 |
| Wall eater | 1 | 3/160 | 0.018750001 | 1229 (0.018753) | 106.667 / 106.649 | 1014 / 1014 |
| Egg thief | 20 | 22/140 | 0.157142863 | 10299 (0.157150) | 12.7273 / 12.7267 | 121 / 121 |
| Egg thief | 1 | 3/140 | 0.021428572 | 1404 (0.021423) | 93.3333 / 93.3561 | 887 / 887 |
| Rocket | 10 (fixed) | 10/25, rounded **up** | 0.400000006 | 26215 (0.400009) | 5.00000 / 4.99989 | 48 / 48 |

- Monsters and robots do not gain health, so their maximum is their initial health; only the player has a different maximum (200). Health 1 is shown as the slowest case.
- A velocity is off by at most half of 1/65536 (at most 0.03% in ticks per tile, at the lowest healths). Over 100 tiles the low-health rows differ by at most 3 ticks in 12,600.
- **The rocket is the one rounded up.** Rounded to nearest, 10/25 is 26214, a hair *below* 0.4, and that moved every rocket's explosion one tick later than with floats (the first version of this change; see item 2). 26215 puts it a hair above, which restores the float timing exactly. A rocket still crosses 100 tiles in 498 ticks both ways.
- **Low-speed rows can shift a tile crossing by one tick:** the player at health 1 takes 692 ticks for ten tiles instead of 691 (and 100 tiles differ by up to 3 ticks in 12,600 for the slowest monsters). The sprite is at most 1 pixel off (about 2 px of the 0.03% error accumulating at most), but the tile the entity belongs to for collisions changes one tick earlier or later, which is a sim-level effect, not only a drawing one.
- The velocity of players, monsters and robots is recomputed from health every tick, as before; the table gives the value at each health.

**Behaviour changes** (all found by measuring the old float code, not predicted; re-measured after the rocket fix)

1. **Self-steering robots, drones and egg thieves make different random turns** (wall eaters did not, in the tests), and **so do AI bots in one place.** `SimpleBrain` turns when it is "within one step of the middle of a tile" (`|offset| <= velocity`), and for a velocity like a robot's 4/19 the offset lands on exactly one step away every 19 ticks. Float noise decided those ties one way; fixed-point decides some of them the other way, so the entity turns one tick earlier or later and, being random, wanders a different path from then on. The pace is the same: tile changes over 1500 ticks were 161/130/160/125/127/90/120/77 before and 161/131/160/125/127/90/120/77 after.
   - The same test decides when a **bot** counts as having arrived on the middle of a tile of its route (`PlayerCharacterEntity::nearTileCenter`, used at the end of a GO_TO in `processNextInstruction`, around line 1000), so a bot can finish a step one tick earlier or later than before. Human players are not affected by this one (their movement is direct input). The A\* routes bots choose are unchanged (item 6 above), but a bot's timing is, so a full round with a given seed no longer plays out identically.
2. **Rockets: no change.** The first version of this conversion rounded every velocity to nearest and made rockets explode one tick (33 ms) later at their target (ticks 1, 5, 10, 15, ... instead of 1, 4, 9, 14, ...; wall and block stops 70 instead of 69). Cause: in every tile after the first, the rocket's offset lands on exactly -0.4 one tick before the middle, and `|offset| <= velocity` was decided by rounding: float gave -0.39999995 against 0.4000000059 (inside), nearest-fixed gave -26216 against 26214 (just outside). The review's decision was to keep the old timing, so the rocket velocity is rounded **up** (`fixedFromRatioCeil`, 26215). Re-measured: the 52-flight rocket digest equals the float code's exactly (`0x0bdb3f6c29ac1b6f`: every explosion tick and tile, in all four directions and for wall stops), and the full per-flight trace is identical value for value. The same digest comes out on GCC and on Clang.
3. **A sprite is drawn one pixel from where the float code drew it on about 0.5% of ticks** (37 of 7200 in the scripted walks; never more than one pixel; 8 of 12 walks identical). This is the 0.03% velocity quantisation reaching a pixel boundary, and for the slowest movers it can also move the tick at which the entity crosses into the next tile by one (player at health 1: 692 ticks for ten tiles, not 691), so the tile it is considered to be in for collisions can change for one tick. It is checked against a float reference model of the old `moveInDirection` kept in the test; that model reproduces all 12 recorded float runs digest for digest.
4. A\* routes: unchanged.

Not a change: the old code never truncated in the rocket check, rendering truncation is identical, an entity stopped by a wall still comes to rest with offset exactly 0, `moveInDirection`'s branch structure is untouched, and the data files are untouched.

**Remaining floats in `src/`**

| Where | What | Why it stays |
|---|---|---|
| `MickSDLUtil.h/.cpp` `rumbleController(..., float strength, ...)` | haptic strength | SDL haptics API; not sim |
| `GameRunningSDLInput.h/.cpp` `rumbleController` passthrough and the `0.75f` at the START-button join | haptic strength | input layer; not sim |
| `PlayerCharacterEntity.cpp:151` `rumbleController(attachedController, 0.5f, 200)` | haptic strength | the one float left in a sim file, excused by one allowlisted line in the guard. Brief 04b moves rumble out of `onCollision`, and the exception goes with it |
| `InfoPanel.cpp` (portrait scale `double`, health-bar intensity and width, portrait x position) | HUD drawing | presentation |
| `MickSDLRenderer.cpp:168` `double scale` | window letterboxing | presentation |
| `MenuSDLRenderer.cpp:155` `ProcessMouseWheel(event.wheel.y, ...)` | an **implicit int to float conversion** at a call into RmlUi (its parameter is a float) | menu input; the text scan cannot see it, so it is not on the guard's allowlist or in `-Wfloat-conversion` output |
| `src/RmlUI/glue/SystemInterfaceSDL2.*` (`double GetElapsedTime`), `RenderInterfaceSDL2.cpp` (`float*` vertices) | RmlUi's interface types | UI library API |
| `src/RmlUI/events/Shell.h`, `src/RmlUI/shell/**` | | dead code, not compiled |

(`GameSettings::mapRatio` was an unused float and was deleted.) Everything under `src/RmlUI` is excluded from the guard as a directory; the files above it are excluded one by one with the reason shown.

**Float-sensitive things left?** Nothing in the sim depends on floating-point behaviour any more, so `-ffp-contract=off` and the `-ffast-math`/`-Ofast` rejection were not added and are not needed for determinism. The rendering and HUD floats are single operations on values that never feed back. Other divergence sources (UB, libstdc++ vs libc++, evaluation order) are still 05b's.

**Tests** (suite 155 to 186; 31 new; the same 186 pass with GCC 13 and with Clang)

- `FixedMathTest.cpp` (20): helper rounding (including `fixedFromRatioCeil` and large-argument results that must not saturate), symmetry, ties, saturation, zero and negative denominators, abs; `fixedToPixels` against the old expression for every offset; the velocity table above through the real classes (initial, max and low health, ticks for ten tiles); negative-health velocity; 240 movement rows from an independent integer model (including starts that land exactly on +-1.0); wall stops; no drift out of range; `nearTileCenter` boundaries for players and `SimpleBrain`.
- `FixedPointSimTest.cpp` (8): characterisation tests written first against the float code (commit `246f469`, all passing there) then updated where the fixed result legitimately differs, as described above: scripted walks (digest plus a tick-by-tick float-model comparison, at most one pixel), self-steering walks, rocket explosion ticks, a rocket created by message exploding in its target tile, a rocket fired by a real `RocketSpinEntity` at an enemy that then leaves still exploding in the enemy's tile, A\* routes on 48 layouts, A\* first steps on an open board.
- `FixedPointGuardTest.cpp` (3): the source scan, the scan's own positives and negatives (a positive case for each of the reviewer's bypasses), and that an excused line is excused only in its own file.
- The first sweep of the new tests found 21 failing seeds, all the "rocket created by message" test (the two bots wandered into the rocket's path on some seeds). It now removes both bots; the sweep numbers below are from the fixed test.
- `GameEntity::pixelX()/pixelY()` were added (the sprite's top-left, used by `render()` and the HUD) so the tests exercise the real pixel expression.

**What was shown to fail without the change.** The new API does not exist before the change, so those tests cannot be compiled against the old code. The characterisation tests were run against the float code first and passed there. The conversion was then reverted piece by piece (mutations applied to the converted code in a scratch worktree, rebuilt, full suite run, restored):

| Reverted / broken | Tests that failed |
|---|---|
| Velocity rounds down (truncates) instead of to nearest | 8: `FixedHelpersTest` ties, symmetry, nearest; `FixedMathTest` robot/drone/wall eater/egg thief velocities and negative-health velocity; the scripted-walk digest, the float-model comparison and the self-steering walks |
| Exact ties round toward zero instead of away | 1: `ExactTiesRoundAwayFromZero` |
| `fixedToPixels` floors (`>>`) instead of truncating | 5: both `ToPixels` tests (including the sweep against the old float expression) and three walk tests |
| `fixedAbs` returns negatives unchanged | 5: abs, both `nearTileCenter` tests, the rocket ticks, the self-steering walks |
| Tile wrap at `>= 1.0` instead of `> 1.0` | 1: `MovementProducesTheExpectedOffsetsAndTileChanges` (only after the +-47514 rows were added; before that nothing noticed) |
| A wall ahead no longer clamps the move (RIGHT) | 4: movement rows, drift test, two walk tests |
| Wrong sign when cornering into an UP move | 4: movement rows and three walk tests |
| Player `nearTileCenter` uses `<` | 1: `PlayerNearTileCenter...` |
| `SimpleBrain::nearTileCenter` uses `<` | 2: `SimpleBrainNearTileCenter...` and the self-steering walks |
| Rocket arrival check drops the abs | 1: rocket explosion ticks |
| `EntityFactory` drops the rocket target | 1: `ARocketCreatedByMessage...` |
| `RocketSpinEntity::fireAt` passes no target | 1: `ARocketFiredByASpinner...` (before that test was added: 0, so nothing noticed) |
| Render pixel conversion floors (in `pixelX()`) | 3 walk tests (before `pixelX()` existed: 0, as `render()` could not be observed) |
| A\* open key = insertion sequence instead of next free number | 1: `PathSearchPicksTheSameRoutes...` (shows the brief's suggestion changes routes) |
| A\* tie-break counts down instead of up | 2: route test and open-board first steps |
| Rocket velocity from `health + 1` | 2: rocket velocity and rocket ticks |
| Rocket velocity rounded to nearest again (26214) | 2: `RocketVelocityIsATenthOver...RoundedUp` and `RocketsExplodeOnTheSameTickAsTheOriginalMaths` (the float digest) |
| Player velocity uses `health + 9` in `update()` | 5: player velocities, negative health, movement rows, two walk tests |
| A `static float` added to `RobotEntity.cpp`; a `double` member in `EntityMessage.h`; `#include <cmath>` in `SimpleBrain.cpp`; a float literal in `DroneEntity.cpp` | 1 each: `NoFloatingPointInSimulationSources` |
| The review's bypasses added to `DroneEntity.cpp`: `(int)hypot(3,4)`, `exp2`, `log2`, `cbrt`, `float_t`, `double_t`, `0x1p-1`, `atof`, `std::stod`, `SDL_FPoint`, and a float after a `'"'` character literal | all 11 caught (passed the guard before the fix round) |


**Cannot fail by construction:** the A\* route tests pass on both representations by design (that is the claim); what proves they can fail is the insertion-order key above. The velocity-table float columns and the float reference model are inputs, not assertions of their own. The negative/zero denominator guard in `fixedFromRatio`: dropping the `< 0` half of the check gives the same result by accident (a huge unsigned divisor yields 0), so that is an equivalent mutation; the `== 0` half, which prevents a divide by zero, is covered by the zero-denominator test.

**Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, from the build directory)

- Before (`claude`, 155 tests): 1 failing seed, **6**: `AISeekEnergyTest.HealthyBotWithAnEggUnderItselfStillGetsOutToAPickupInTime`, the known bot livelock in `TODO`.
- After (186 tests, after the review fix round): 1 failing seed, **6**, the same test, with the same message (`bot never got out, stuck at (1,1)`) and the same round seeds as on `claude`, so it is the known livelock and not the conversion (the bot is boxed in at its start tile, where no movement maths is involved). Nothing else fails on any of the 200 seeds. (An earlier sweep that showed 21 failing seeds was my own new rocket-by-message test, fixed before the numbers here.)
- An AddressSanitizer + UBSan build of the whole suite (the recipe in `tests/README.md` with `,undefined` added) is clean: 186 pass, no reports.
- The same 186 tests pass when built with Clang (including the rocket digest, so it is the same on both compilers).

**Deviations from the brief**

- The type is `fixed_t`, not `fixed` (name clash with `std::fixed`).
- The rocket velocity is rounded **up**, not to nearest (review decision, to keep float timing); the brief's rounding rule applies to every other velocity.
- The A\* tie-break is the next free number, not insertion order (see item 6); the brief's wording was imprecise.
- `abs(m_offsetX)` was already the float overload, so there was nothing to preserve (brief's check item).
- `GameSettings::mapRatio` was deleted rather than allowlisted.
- The guard scans all of `src/` minus an explicit non-sim allowlist, rather than a hand-maintained list of sim files, so a new file is held to the rule by default.
- `stripCommentsAndStrings` moved from `SimRandomTest.cpp` to `tests/support/SourceScan.h`, now shared with the new guard (it also keeps the line breaks inside block comments).
- No `GameEngine` change. `main.cpp`: only an unused `#include <math.h>` removed (the guard flagged it).
- Fix round: the project is pinned to C++17 (`CMAKE_CXX_STANDARD`, extensions off) for every target; `-Wfloat-conversion` is on for the library.

**Follow-ups**

- **03a:** the state hash can cover `m_offsetX/Y` and `m_velocity` as plain `int32_t`s. `m_velocity` is a pure function of health for players and monsters (recomputed each tick), so it need not be hashed if health is.
- **04b:** removing rumble from `onCollision` lets the allowlisted line in `FixedPointGuardTest.cpp` go; the test fails if the line is gone but the entry remains.
- **03b:** point `FixedPointSourceGuardTest` at the `bitriot_sim` sources instead of `src/` minus the allowlist.
- **03b: an object-level float check.** The text scan cannot see an `auto` that carries a double, or an implicit int to float conversion at a call. When `bitriot_sim` exists, add a check on its compiled objects (e.g. `objdump -d` for `cvtsi2s[sd]`, `cvtts[sd]2si`, and `...ss`/`...sd` arithmetic, or a link-time check) so the guarantee does not rest on source text alone. (`-Wfloat-conversion` is on for the library and catches float to int narrowing, e.g. `setOffsetX(0.5f)`.)

**Play-test** (needs a display; not run in the cloud container). Build the game on Arch Linux **from `claude` once this branch is merged**, and the version before this change next to it (commit f3c45f6) to compare. The commands are untested on Arch (the container is Ubuntu): RmlUi 4.3 is not packaged for Arch, so it is built from source as `scripts/cloud-setup.sh` does, with two flags Arch's newer tools need. RmlUi 4.3 declares `cmake_minimum_required(VERSION 3.1)`, which CMake 4.x refuses unless told a minimum policy version, and GCC 15 no longer pulls in `<cstdint>` transitively, so it is forced in (the same workaround BitRiot's own CMakeLists applies).

```bash
# one-time: dependencies
sudo pacman -S --needed base-devel cmake git sdl2 sdl2_mixer sdl2_ttf sdl2_image freetype2 mesa
git clone --depth 1 --branch 4.3 https://github.com/mikke89/RmlUi.git /tmp/RmlUi
cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_SAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_LUA_BINDINGS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"
cmake --build /tmp/RmlUi/build -j"$(nproc)" && sudo cmake --install /tmp/RmlUi/build && sudo ldconfig

# AFTER: claude, after this branch is merged
git clone --branch claude https://github.com/VenKamikaze/BitRiot.git ~/bitriot-after
cmake -S ~/bitriot-after -B ~/bitriot-after/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-after/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-after/build && ./BitRiot)

# BEFORE (for comparison): the commit before this change, f3c45f6
git clone https://github.com/VenKamikaze/BitRiot.git ~/bitriot-before
git -C ~/bitriot-before checkout f3c45f6
cmake -S ~/bitriot-before -B ~/bitriot-before/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-before/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-before/build && ./BitRiot)
```

To play before the merge, use `--branch netplay/02b-fixed-point` in the AFTER clone (the branch is on origin). `./BitRiot -b` plays on the big 25x19 map; `-f` asks for fullscreen. Play a few rounds with at least one human and several bots, with the spawner on (it makes robots, drones, wall eaters and egg thieves). Compare with the BEFORE build where useful, and look at:

1. **Players:** top speed along a corridor at the start of a round (about 4 tiles a second at 100 health), turning into a side corridor (the player should slide onto the corridor line smoothly, as before), stopping flush against walls and blocks with no jitter or creeping, and speed after eating energy (faster) and after being hurt (slower).
2. **Robots, drones, wall eaters, egg thieves:** speed relative to a player; that they still turn at junctions and do not stop dead or dither in a corridor; wall eaters still pass through blocks; egg thieves still steal eggs.
3. **Rockets** (a player laying a rocket spinner, or a robot that fires rockets): flight speed along a corridor, and that the rocket still explodes on the target tile (the timing is meant to be identical to before).
4. **Bots:** they still path around blocks and bomb through them (same A\* routes as before; a step can finish a tick earlier or later).
5. Anything that looks one pixel off at a tile boundary is the expected quantisation, not a bug.

**Play-test result:** *(pending)*
