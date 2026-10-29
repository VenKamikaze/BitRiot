# 05 — Match record, replay and `bitriot_headless`

- **Repo:** BitRiot
- **Branch:** `netplay/05-replay`
- **Depends on:** 03b, 04b
- **Status:** done

## Goal

A round is fully described by four things: `MatchConfig`, a hash of the game data, a build id, and the sequence of `TickInputs`. Record that in a small file, replay it headlessly, and prove the replay reproduces the `stateHash()` of every tick. This is the core test tool for all the networked work, because lockstep netplay is the same replay fed live.

The cross-platform check built on this is brief 05b.

## Read first

- The Outcome sections of briefs 02, 03a, 03b, 04a and 04b. Use the PRNG, `MatchConfig`, `Simulation`, `stateHash()`, `bitriot_sim` and `TickInputs` as they actually landed.
- [`research/code-facts.md`](research/code-facts.md) §7 (data files: a missing key silently returns 1).

## In scope

1. **`MatchRecord` format: one record per round.**
   - **Encoding:** versioned binary, little-endian, fixed-width ints, so JS and Go tools can read it.
   - **Header:**
     - magic and format version
     - **build id**: a hash of the sim source, or the git commit, generated at build time
     - **data hash** (item 2)
     - `MatchConfig`
   - **Body:** the `TickInputs` for every tick (byte bitmasks plus control events), with an optional `stateHash` checkpoint every K ticks (for example K = 30).
   - Use simple run-length compression of identical ticks, but keep it optional.
   - **Scripted inputs reuse this format.** `bitriot_headless` takes a record, or just its input stream, as the script. Don't invent a second format.
   - **Document the format** in `doco/netplay/match-record-format.md`.
2. **Data hash.**
   - Load `data/entity.txt` and `data/player.txt` eagerly at start-up. `DataReader` is lazy today.
   - Hash the **parsed, normalised key=value pairs**, sorted by key. Don't hash raw bytes: git autocrlf on Windows would make two identical files differ.
   - Also include sim constants that aren't in data files, and list them.
   - Expose it as `Simulation::dataHash()`. Netplay (07) puts the data hash and build id in `HELLO` and rejects any mismatch.
3. **Recorder.**
   - Local play can record each round with `--record <dir>`, giving one file per round.
   - This needs `getopt_long` (code-facts §12).
   - Recording must not change gameplay.
4. **`bitriot_headless`.**
   - A CMake target that links `bitriot_sim` only (03b): no window, no audio, no bitmaps.
   - Commands:
     - `replay <file>`: verify the checkpoints and report the first mismatching tick.
     - `run --config … --seed … [--script <file>] --out <file>`: play a fresh round with AI and scripted inputs and write a record.
     - `dump <file> --tick N`: print a readable per-entity state listing for tick N, so dumps from two diverging runs can be diffed.
   - Keep the command-line interface clean. Briefs 05b, 07 and 13 reuse it.

## Out of scope

- Networking.
- Watching replays with rendering.
- Cross-compiler, aarch64 and wasm checks (05b).

## Tests

- **Round-trip:** a seeded round with scripted human inputs is recorded, replayed, and matches on every tick. Serialisation is byte-identical after a round-trip.
- **Golden record.** Commit one under `tests/data/`.
  - It must be **one complete round on a small real map, with blocks**. Script human use of both actions in every direction, alongside AI players, so pathfinding, rockets, robots, egg hatching and explosions are all exercised. A few hundred ticks barely gets past the AI's opening WAIT, so it is not enough.
  - Replaying it must match its checkpoints.
  - This test intentionally breaks on any sim behaviour change.
  - Document in `tests/README.md` how to regenerate it deliberately, and require that the commit message calls out any regeneration.
- **Mismatch detection:** corrupt one input, and the replayer names the first diverging tick.
- **Multi-process:** two `bitriot_headless` processes replaying the same record give identical output.
- **Debug and Release:** the golden replay matches in both builds.

## Acceptance criteria

- Record → replay is bit-exact on every tick.
- A golden full-round record is in the suite, with documented regeneration.
- `bitriot_headless` has `run`, `replay` and `dump`.
- The data hash and build id are defined.
- The format doc and `tests/README.md` are written.

## Outcome

**Status: done.** A round can be recorded, replayed and inspected: the record format and its document, the data hash, the build id, the recorder (`--record <dir>`), `bitriot_headless` (`replay`, `run`, `dump`, `version`), the golden full-round record, and every test in the brief. No play-test is needed: recording only reads the simulation, and the one change to the game's own behaviour (the command line, below) is covered by tests and was exercised in the real game under Xvfb. The **six golden digests in `SimulationTest` are byte-identical** (nothing hashed changed, and `BITRIOT_HASH_DUMP` was not needed).

The full specification is [`match-record-format.md`](match-record-format.md); this section is what landed, how it was checked, and what the later briefs need.

**What landed**

1. **`MatchRecord`** (`src/MatchRecord.{h,cpp}`, in `bitriot_sim`). One record per round: magic `BRMR`, format version 1, flags (RLE, ROUND_OVER), state-hash version, checkpoint interval, **build id** (u64), **data hash** (u64), the `MatchConfig` (seed, map, block percentage, spawner, per slot team and AI flag), then the ticks (one `PlayerInput` byte per slot plus a count of control events and `{slot, kind}` pairs), optionally run-length encoded (`u16 count` + one tick, maximal runs only, so the encoding is unique), then the checkpoints (`u32 tick`, `u64 hash`). Little-endian, fixed-width everywhere. `parse` refuses everything malformed (every truncation, trailing bytes, bad counts, a tick count over 4,194,304 so a hostile file cannot make a reader allocate gigabytes, non-maximal runs, out-of-order checkpoints) and **`serialize(parse(x)) == x` byte for byte**. `MatchRecorder` builds a record as a round is stepped (a checkpoint at tick 0, every K ticks, and the last).
2. **`Simulation::dataHash()`** (`src/DataHash.*`): FNV-1a 64 over the sorted `name=value` items of the **parsed** `data/entity.txt` and `data/player.txt` plus eleven named sim constants that live in code (listed in the format document and `simConstantItems()`). **Data loading is eager**: `Simulation::start()` parses both files first (before touching anything) and throws if it cannot, instead of every constant answering 1. `DataReader` gained `loadFile()` and `normalisedEntries()`, and now **strips carriage returns before parsing** (a CRLF file did not parse on Linux; this was needed to make "line endings do not change the hash" true and testable).
3. **Build id** (`src/BuildId.h`, `cmake/GenerateBuildId.cmake`, target `bitriot_build_id`): MD5 (first 8 bytes) of the repository-relative path and CR-stripped text of every file of `bitriot_sim` plus the project headers they include, regenerated on every build and rewritten only if changed. Same on GCC and Clang (checked: `0x40ac671e1cc35d3b` on both); core-only files do not move it.
4. **Replayer** (`src/MatchReplay.{h,cpp}`): `replayRecord()` returns a status, never throws for a bad record. A checkpoint mismatch names the **first diverging checkpoint tick and the last agreeing one**; with a checkpoint on every tick (the golden record) that is the exact tick. Also `DATA_HASH_MISMATCH`, `HASH_VERSION_MISMATCH`, `BAD_CONFIG`, `SIM_ERROR` (the tick the simulation refused), `ROUND_END_MISMATCH`. `recordRound()` plays a fresh round, scripted or not, into a record.
5. **`Simulation::dumpState()`**: a readable listing of every hashed field of every entity (it is the hash's own traversal with a printing sink, so it cannot drift from what is hashed), the map and the tile lists.
6. **`bitriot_headless`** (`src/headless/`, links `bitriot_sim` only; `ldd` shows no SDL, tested): `replay <file> [--hashes] [--ignore-data-hash]`, `run --out <file> [--config SPEC] [--seed N] [--script FILE] [--max-ticks N] [--checkpoint-every K] [--no-rle]`, `dump <file> [--tick N]`, `version [--items]`. Exit codes 0 ok, 1 mismatch, 2 usage or bad file, 3 other game data. **stdout is the same on every build for the same input** (the simulation's DEBUG logging goes to stderr in this binary); build-dependent notes go to stderr. A script is a record file: no second format. The commands are functions in a small library (`bitriot_headless_cli`) so tests call them in-process.
7. **`--record <dir>` and `getopt_long`** (`src/GameOptions.*`; `GameEngine::setRecordDirectory`): one `round-<UTC date>-<UTC time>-seed<seed>.brmr` per round, written when the round ends or is quit, and when the window is closed mid-round; a checkpoint every 30 ticks, RLE on. The recorder reads the simulation and nothing else, and an error in it is logged and never stops the game. Also `-f/--fullscreen`, `-b/--big-map`, `-h/--help`.
8. **Golden record** `tests/data/golden-round.brmr`: **2,768 ticks, 39,791 bytes, a checkpoint on every tick**; a 13x11 map (the smallest in the menu) with 55% blocks, spawner on, four players. Two humans are scripted (`tests/support/GoldenScript.h`) to use ACTION1 and ACTION2 with no direction and with each direction, in a shuffled order that visits all ten gestures before repeating; at tick 400 a bot is taken over by a human, at 700 it is given back, at 1500 player 1 leaves. The round ends naturally (ticks 2,768 including the 60-tick countdown). `GoldenRecordTest` asserts what it exercises: all ten gestures; the humans laid blocks, all five eggs, bombs, mines and rocket spinners; rockets flew; explosions; robots, drones, wall eaters, egg thieves, energy drops and power-ups; damage and munch effects; **175 A\* path searches** by the bots; both humans alive until tick 801; the takeover, return and leave happened. (`BITRIOT_GOLDEN_STATS=1` prints these.) Regeneration is in `tests/README.md` (`BITRIOT_REGEN_GOLDEN=1`, or `bitriot_headless run --script` to keep the inputs); **a regeneration needs the commit message to say so**.

**Tests.** `bitriot_sim_tests` 315 to **398**, `bitriot_tests` 411 to **503** (83 new in both, 6 more in the core executable alone, and 3 that came with the merged fix round), identical counts on GCC Release, Clang Release and GCC Debug; all pass (the merge of `claude` at 0557976, the 04a/04b fix round, is in). New: `MatchRecordTest` (16), `ReplayTest` (22), `GoldenRecordTest` (5), `DataHashTest` (11), `BuildIdTest` (9), `HeadlessCliTest` (20, the real binary), `GameOptionsTest` (6, core only). **Debug and Release both replay the golden record**, and so does Clang; `bitriot_headless replay --hashes` is byte-identical across GCC Release, GCC Debug and Clang Release.

A `-DBITRIOT_BUILD_GAME=OFF` configure (with the SDL2, RmlUi and OpenGL packages made undiscoverable) builds `bitriot_headless` and `bitriot_sim_tests`: 398 pass, and the golden record replays.

**Multi-process.** Two `bitriot_headless` processes started together print identical `replay --hashes` and `dump` output, and two `run`s write identical files (`HeadlessCliTest`).

**Real game.** Under Xvfb with a scratch autoplay hook (not committed): a 2-bot 13x11 round with seed 4242 and `--record` wrote a 549-byte file that `bitriot_headless replay` accepts and that is **byte-identical** to `bitriot_headless run --seed 4242 --config players=2,map=13x11` (1,146 ticks, the same as 03a's manual check). ESC at tick 200 on two consecutive rounds wrote two partial records (`round over: no`) that replay. Bad options exit 2, `-h` exits 0, an unusable `--record` directory exits 2 at start-up.

**What was shown to fail without the change.** The new API does not exist before the change, so the new tests cannot be compiled against the old code. Instead 39 mutations were applied one at a time, the suite rebuilt and run, and the file restored (the harness is a `/tmp` script, not committed). 36 were killed at once; of the other three, two survivors each got a test and were re-run (killed), and one is an equivalent mutant:

| Mutation | Killed by |
|---|---|
| Recorder: no tick-0 checkpoint; checkpoints off by one tick; stores the previous tick's inputs | 7 / 4 / 15 tests (`ReplayTest.CheckpointsAreTaken...`, the corrupted-input and golden tests) |
| **Recording changes play** (a checkpoint turns the NPC spawner off) | 17 tests, among them `ReplayTest.RecordingDoesNotChangeTheRound` and the golden-script tests |
| Replay: mismatch ignored; wrong tick reported; data hash unchecked; round-end flag unchecked; events dropped; one tick late; a sim error reported as OK | 9 / 6 / 2 / 1 / 8 / 18 / 1 tests |
| `recordRound` stops a tick early | 5 |
| Serialiser: runs not maximal; byte order of a u32 swapped; slot input beyond the players allowed | 13 / 21 / 1 |
| Parser: non-maximal runs, trailing bytes, the tick cap, checkpoint order (read side) accepted | 2 / 2 / 2 / 1 (**the last survived** until `CheckpointsMustBeInOrder...` got the read-side cases) |
| Data hash: sim constants left out; second file left out; ints written differently; **sorted on the finished text** | 10 / 11 / 10 / 1 (**the last survived**: the shipped data has no pair of names where the two orders differ, so `ANameThatIsAPrefixOfAnother...` adds one) |
| Eager loading removed from `start()`; carriage returns not stripped by `DataReader` | 2 / 2 |
| Build id: includes not followed; an unchanged file rewritten | 3 / 1 |
| Build id: carriage returns kept | **equivalent mutant**: CMake's `file(READ)` already drops them (length 4 for `a\r\nb\r\n`), so the explicit strip is a guard for versions that do not |
| `dumpState`: entity fields not printed | 1 |
| `bitriot_headless`: replay exits 0 on a mismatch; `--hashes` silent; `run` ignores the script; `dump` ignores `--tick` | 2 / 1 / 2 / 3 |
| **Simulation changes** (A2+UP selects EGG4; bots' `MIN_HEALTH_TO_LAY` 15 to 16; LEAVE treated as a takeover) | the golden record fails each time: `TheGoldenRecordReplaysOnEveryTick` and `...IsWhatTheScriptPlaysToday` (3 / 2 / 3 tests) |
| `bitriot_headless` leaves the logger on stdout (**Debug build only**) | `ReplayOfTheGoldenRecordMatches` (it passes in Release either way, so it was checked in the Debug tree) |
| `GameOptions`: `--record` argument dropped | 3 |

**Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, from the build directory, GCC Release): before (`claude` 84f19b1) `bitriot_tests` (411) **0 failing seeds**, `bitriot_sim_tests` (315) **0**; after `bitriot_tests` (503) **0 failing seeds**, `bitriot_sim_tests` (398) **0**.

**Deviations and decisions** (ambiguities took the conservative option)

- **Build id is a hash of the simulation's source, not the git commit**: it works from a tarball, ignores commits that touch nothing the simulation reads, and is the same on every compiler and OS. It is a CMake custom target, not a configure-time value, so it cannot go stale.
- **The build id is not enforced on replay** (a note on stderr only); the **data hash is**. The golden record is allowed to outlive the build that wrote it, which is what makes "this test breaks on a behaviour change" about behaviour and not about every source edit.
- **Checkpoints are an explicit list** of `(tick, hash)`, not "every K ticks by construction", so a record can have a checkpoint at the last tick, none, or one on every tick. The golden record uses **K = 1** so the replayer names the *exact* diverging tick (the brief asks for "the first diverging tick"); with K = 30 it names the window `(last good, first bad]`, and says to re-record with `--checkpoint-every 1` for the exact tick.
- **`PICKUP_SEARCH_RANGE`** was a function-local `static const int RANGE = 4`; it is now a file-scope constant of the same value so the data hash can list it, and `POOL_POINTS_PER_PLAYER` names the literal 100 in `Simulation::start`. No behaviour change (the digests prove it). Numbers in the simulation that have no name (the bots' 37 and 67, and others) are **not** in the data hash; the build id covers them. The format document says so.
- **`DataReader` strips carriage returns** (above). It is a fix to a latent bug, not a behaviour change for any file that parsed before.
- **The command line changed slightly**: `-h` and `--help` print the usage and exit 0 (before, `-h` fell into the same branch as an unknown option and the game started), and an unknown option or a stray argument prints the usage and exits 2 (before, the game started anyway). `--fullscreen` and `--big-map` are new long names for `-f` and `-b`.
- **A record holds no input for a slot beyond the config's players**: the simulation ignores such bytes, so `serialize` refuses them and `recordRound` zeroes them (a script made for a 6-player round can run a 4-player one). Teams are stored as one byte (0-255), a map side as 16 bits, the block percentage as an `i16`.
- **`--config` lists are colon-separated, slots are 0-based, defaults are 2 players 17x13 55% spawner on**, and the config is checked with the loose `runnableProblem()` (small test maps work). `run` needs `--seed` unless a script supplies one. `--max-ticks` defaults to 100000, so a stalled round (the known bot livelock) ends the run with `round over: no` and exit 0 instead of hanging; a caller that needs a finished round checks the output.
- **One `Simulation` per process** still holds, so `headless::run` called in-process while a test fixture's simulation is alive reports a usage error; the CLI tests use the real binary for that reason.
- `bitriot_headless`'s own arguments are parsed by a small hand-written parser, not `getopt_long` (no global state, the same on Windows and wasm). The game uses `getopt_long` as the brief asked.
- No brief fact was wrong. The brief says "Load ... eagerly at start-up" and "`DataReader` is lazy today": the audio file is still read lazily by `GameSound` (not sim data); only the two sim files are loaded eagerly.

**Follow-ups**

- **05b (cross-platform gate):**
  - Build `bitriot_headless` with `-DBITRIOT_BUILD_GAME=OFF -DBITRIOT_BUILD_TESTS=OFF` (the target needs only `bitriot_sim`; the build-id step runs the host's `cmake -P`, so `emcmake` and a cross build are fine). It reads `data/` from the working directory and nothing from `assets/`; for wasm use `--preload-file data` or `-sNODERAWFS=1` as the brief says. The binary uses `<fstream>`, no threads, no `std::filesystem`.
  - The gate is `bitriot_headless replay --hashes tests/data/golden-round.brmr` under each variant, diffing **stdout** against a reference (stdout is build-independent; the build id appears on stderr only when the record's differs). `bitriot_headless version` must print the **same build id and data hash** on every variant (it does on GCC and Clang); a difference there means the variants are not built from the same source or data, not a determinism fault. Exit code 1 names the first diverging tick (the golden record has a checkpoint on every tick).
  - The negative control can be done without a test-only flag in the simulation: perturb one input of a copy of the golden record (`MatchRecord::readFile`, edit a tick, `writeFile`) and `replay` must exit 1 naming that tick; or `--ignore-data-hash` against edited data. A perturbation of a sim value would still need a flag.
  - The sweep dump `BITRIOT_HASH_DUMP=all` still exists; the golden record adds a human-and-events round to the gate that has a hash on every tick.
  - The object-level float scan (`SimLibraryTest`) and the source guards now also cover `DataHash.cpp`, `MatchRecord.cpp`, `MatchReplay.cpp` and `BuildId.cpp` (they are in `SIM_SOURCES`); `BuildIdValue.cpp` (generated) is in the list too.
- **07 (lockstep):**
  - `HELLO` carries `Simulation::buildId()` and `Simulation::dataHash()` (both u64) and rejects a mismatch of either; `bitriot_headless version --items` lists the data hash's items for the error message. Validate the host's `MatchConfig` with `problem()` (strict) before `start()`, and keep local settings with `GameSettings::snapshot()` as 03a said.
  - Keep a `MatchRecorder` per round on every peer: call `onStep(inputs, sim)` with the **final** `TickInputs` the host decided (not the local sample), and a checkpoint every K ticks doubles as the desync check (`stateHash()` at agreed ticks). `Simulation::dumpState()` is the ring-buffer dump on a `DESYNC`: write it with the record.
  - The writer refuses an input byte for a slot beyond the config's players and more than 255 control events in a tick; the host should reject or zero those before broadcasting (the simulation ignores the first and `step()` already refuses a bad event).
  - Extend `bitriot_headless` with `--host` / `--join` and keep the command names, the exit codes (0, 1, 2, 3) and the stdout-is-deterministic rule (`headless::run(args, out, err)`).
- **13 (disconnects, hot-join):**
  - A joiner fast-forwards with `replayRecord()` (`ReplayOptions::stopAfterTick`, `onTick`); no rendering or sound is involved in `bitriot_sim` (sound is a silent default), so the "mute" is automatic in the headless path. Speed, GCC Release on this container (shared with a running sweep): the 2,768-tick golden round replays in **about 50 ms with a hash on every tick** (roughly 55,000 ticks a second), so 9,000 ticks is about a sixth of a second natively. wasm still has to be measured (05b's build can).
  - A record's counts are in its header, so a host cannot stream a growing file: serialise the record at the moment of the join (`MatchRecorder::record()` then `serialize()`), send it in chunks, and send later ticks separately. `ROUND_OVER` is false for a round in progress; `parse` accepts a record with no checkpoints or a final tick that is not a checkpoint.
  - `LEAVE` and `HUMAN_TAKES_OVER` events are in the format already (the golden record has all three).
- **Not done, found on the way:** `GameSound` still reads `data/audio.txt` lazily (not simulation data). The scratch autoplay hook used to check `--record` in the real game is not committed; if a regular check of the engine's record path is wanted, an environment hook like it belongs in a test-only build. `PROMPT.md` still says "four pinned rounds" (there are six).
