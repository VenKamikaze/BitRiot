# 02b — Fixed-point simulation maths

- **Repo:** BitRiot
- **Branch:** `netplay/02b-fixed-point`
- **Depends on:** 01, 02
- **Status:** todo

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

*(fill in when done)*
