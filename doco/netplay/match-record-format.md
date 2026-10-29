# Match record format, data hash, build id and `bitriot_headless`

Written for brief 05. This is the specification: the C++ in `src/MatchRecord.*`, `src/DataHash.*`, `src/BuildId.h` and `src/headless/` implements it, and `tests/MatchRecordTest.cpp` pins the bytes. A reader in another language (a replay viewer in JS, a relay-side tool in Go) can be written from this page alone.

A round is fully described by four things: the **`MatchConfig`**, a hash of the **game data**, a **build id**, and the sequence of **`TickInputs`**. A record is those four, plus **checkpoints** (the state hash every K ticks) that prove a replay played the same round.

- [The record file](#the-record-file)
- [Reading and writing rules](#reading-and-writing-rules)
- [The data hash](#the-data-hash)
- [The build id](#the-build-id)
- [Checkpoints and what a replay can tell you](#checkpoints-and-what-a-replay-can-tell-you)
- [`bitriot_headless`](#bitriot_headless)
- [`--record` in the game](#--record-in-the-game)
- [Versioning](#versioning)

## The record file

**One record per round.** The file extension is `.brmr`. Everything is **little-endian** and **fixed-width**: no varints, no padding, no alignment. `u8`/`u16`/`u32`/`u64` are unsigned; `i16` is two's complement.

### Layout

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic | the bytes `B R M R` (`42 52 4D 52`) |
| 4 | u16 | format version | `1` |
| 6 | u16 | flags | bit 0 `RLE` (the body is run-length encoded), bit 1 `ROUND_OVER` (the round had ended after the last tick). All other bits must be 0 |
| 8 | u32 | state hash version | `Simulation::STATE_HASH_VERSION` of the build that took the checkpoints (2 today). A replay whose build writes another version cannot compare checkpoints |
| 12 | u32 | checkpoint interval | the K the recorder was asked for; informational (the checkpoint list is what counts). 0 = no periodic checkpoints |
| 16 | u64 | build id | see [The build id](#the-build-id) |
| 24 | u64 | data hash | see [The data hash](#the-data-hash) |
| 32 | u64 | seed | `MatchConfig::seed` |
| 40 | u16 | map width | tiles |
| 42 | u16 | map height | tiles |
| 44 | i16 | block percentage | `MatchConfig::blockPercentage` (the sim clamps to 0-100) |
| 46 | u8 | NPC spawner | 0 or 1 |
| 47 | u8 | player count *n* | 0 to 6 (a *config* must have 2-6 to be played, but the file format only refuses more than 6) |
| 48 | 2 × *n* | slots | per slot, in slot order: `u8 team`, `u8 ai` (0 = starts as a human, 1 = starts as a bot) |
| 48 + 2*n* | u32 | tick count *T* | at most 1,048,576 (`MAX_TICKS`, about 9.7 hours at 30 ticks a second) |
| +4 | u32 | checkpoint count *C* | at most *T* + 1 |
| +4 | u32 | body length *B* | bytes |
| +4 | *B* | body | the ticks, below |
| +*B* | 12 × *C* | checkpoints | per checkpoint: `u32 tick`, `u64 hash` |

The file ends after the last checkpoint. Trailing bytes are an error.

**Slot is `playerNumber - 1`**, 0-based, everywhere: slot 0 is player 1.

### A tick

`ticks[i]` is the input of tick *i* + 1 (the (*i*+1)-th `Simulation::step` after `start`). One tick is:

| Size | Field |
|---|---|
| *n* × u8 | one `PlayerInput` byte per slot, slot 0 first |
| u8 | control event count *E*, at most 24 (`MAX_EVENTS_PER_TICK`: four per player slot) |
| 2 × *E* | per event: `u8 slot`, `u8 kind` |

A `PlayerInput` byte is the one in `src/TickInputs.h`: bits 0-5 are the held buttons (UP, DOWN, LEFT, RIGHT, ACTION1, ACTION2), bit 6 is ACTION1 released, bit 7 is ACTION2 released (an edge since the last sample). A control event `kind` is `0` HUMAN_TAKES_OVER, `1` RETURN_TO_AI, `2` LEAVE (stable values; see `ControlEvent`). The events of a tick are applied in order, before that tick's inputs. A record holds no input for a slot beyond *n*: the simulation ignores such bytes, so the writer refuses them.

### The body

- **Without `RLE`:** *T* ticks, one after another.
- **With `RLE`:** a sequence of **runs** until *T* ticks are covered: `u16 count` (1 to 65535) followed by one tick, meaning that tick repeated *count* times. Two ticks are the same when their input bytes and their events are all the same.
- **Canonical form.** A writer must emit **maximal** runs: a run is never followed by a run of the same tick, except that a run longer than 65535 is split at 65535. A reader refuses a non-maximal encoding. This keeps the encoding unique, so `parse` then `serialize` gives back exactly the bytes read, which the tests rely on.

The body must use exactly *B* bytes.

### Worked example

Two players, two ticks, no RLE, one checkpoint (this is `MatchRecordTest.TheBytesAreTheDocumentedLayout`):

```
42 52 4D 52              magic
01 00                    format version 1
02 00                    flags: ROUND_OVER
02 00 00 00              state hash version 2
1E 00 00 00              checkpoint interval 30
18 17 16 15 14 13 12 11  build id 0x1112131415161718
28 27 26 25 24 23 22 21  data hash 0x2122232425262728
38 37 36 35 34 33 32 31  seed
0E 0D                    map width 0x0D0E
0C 0B                    map height 0x0B0C
FE FF                    block percentage -2
01                       spawner on
02                       two players
05 00                    slot 0: team 5, human
07 01                    slot 1: team 7, AI
02 00 00 00              two ticks
01 00 00 00              one checkpoint
08 00 00 00              body: 8 bytes
11 22 00                 tick 1: inputs 0x11, 0x22; no events
33 44 01 01 02           tick 2: inputs 0x33, 0x44; one event: slot 1, LEAVE
02 00 00 00              checkpoint: tick 2
48 47 46 45 44 43 42 41  checkpoint: hash 0x4142434445464748
```

## Reading and writing rules

**A reader** (`MatchRecord::parse`) checks, and refuses with a message naming the byte offset where it can:

- the magic, the version (anything other than 1 is refused, not guessed at), unknown flag bits;
- a player count above 6, a spawner or AI byte other than 0 or 1;
- a tick count above `MAX_TICKS`, more than 24 events in a tick, or more than 65,536 events in the whole record (`MAX_TOTAL_EVENTS`), counting a repeated run's events once per repeat and **checked before the run is expanded**. A run-length encoded tick is copied into every tick of its run, so without these a few hundred bytes could stand for gigabytes; with them the most a file can make a reader hold is about 33 MB of ticks plus 3 MB of events;
- a checkpoint count above *T* + 1, and a body length and checkpoint count that do not add up to exactly the rest of the file;
- a run length of 0 or one that overshoots *T*, a non-maximal run, a body that does not end where *B* says;
- checkpoints that are not strictly increasing or lie beyond tick *T*.

It does **not** check that the record can be *replayed*: an event for a slot the round does not have, or a config the simulation will not start, is for the replayer to report (as `SIM_ERROR` / `BAD_CONFIG`), with the tick. Offsets in error messages and flag masks are written in hex (`byte 0x34`).

**A writer** (`MatchRecord::serialize`) refuses what the format cannot hold: a team outside 0-255, a map side above 65535, a block percentage outside an `i16`, more than 6 players, more than 24 events in a tick or 65,536 in the record, an event slot or kind above 255, a non-zero input byte for a slot beyond *n*, out-of-order checkpoints.

**Checkpoint policy** (`MatchRecorder`): a checkpoint at tick 0 (straight after `start`), at every multiple of the interval, and at the last tick. Interval 0 means only the first and the last. A checkpoint `(t, h)` is `stateHash()` after *t* steps. Reading the hash changes nothing about the round (`ReplayTest.RecordingDoesNotChangeTheRound`).

**Scripted inputs reuse this format.** `bitriot_headless run --script <file>` takes a record file as its script: its ticks are the inputs (and events) of a fresh run. There is no second script format. A hand-written script is a record with no checkpoints.

**Size.** One byte per slot per tick plus one: 9,000 ticks of six players is 63 KB without RLE, much less with it. The golden record (2,768 ticks, four players, a checkpoint on every tick) is 39,791 bytes.

## The data hash

`Simulation::dataHash()` (`src/DataHash.*`): one 64-bit number saying "same game data, same tuning constants". Netplay (07) puts it in `HELLO` and rejects a mismatch; a record carries it and a replay refuses a record made on other data (exit code 3, not a "mismatch at tick N").

**What is hashed.** A list of `name=value` items, sorted by name, from two sources.

1. **Every constant of `data/entity.txt` and `data/player.txt`, after parsing** (`DataReader`). The name is `data/entity.txt:BOMB_FUSE`; the value is an int in decimal (`045` and `45` are the same constant) or a string as written. **The values are hashed after `DataReader`'s parsing, not as text.** The raw bytes are never hashed, so comments, blank lines, the order of lines, tabs against spaces and line endings (git autocrlf on Windows) do not matter. `DataReader` removes carriage returns and a leading UTF-8 byte order mark before it parses, so a CRLF file or one saved with a BOM loads the same everywhere. The other data files (`renderer.txt`, `audio.txt`, `messages.txt`, `map.txt`) are not simulation data and are not hashed.
2. **The simulation's named tuning constants that are in code, not in a data file**, named `sim:<Class>.<NAME>`:

   | Item | Value today |
   |---|---|
   | `sim:Simulation.STATE_HASH_VERSION` | 2 |
   | `sim:Simulation.POOL_POINTS_PER_PLAYER` | 100 |
   | `sim:GameOverTimer.GAME_OVER_COUNTDOWN_TICKS` | 60 |
   | `sim:GameSettings.MAX_PLAYERS` / `MIN_PLAYERS` | 6 / 2 |
   | `sim:Fixed.FIXED_SHIFT` | 16 |
   | `sim:SimpleBrain.INITIAL_MIN_MOVE_TIMER` | 5 |
   | `sim:PlayerCharacterEntity.MIN_HEALTH_TO_LAY` | 15 |
   | `sim:PlayerCharacterEntity.MIN_HEALTH_AFTER_OWN_BLAST` | 3 |
   | `sim:PlayerCharacterEntity.PICKUP_RETRY_FRAMES` | 600 |
   | `sim:PlayerCharacterEntity.MAX_PICKUP_ATTEMPTS` | 3 |
   | `sim:PlayerCharacterEntity.PICKUP_SEARCH_RANGE` | 4 |

   A numeric literal in the simulation's code that has no name (the bots' check timers 37 and 67, say) is **not** in the data hash; it is covered by the build id. When a named constant is added to the simulation, add it to `simConstantItems()` in `src/DataHash.cpp`.

**Algorithm.** Sort the items bytewise by name (unsigned byte order, on the name alone; names are unique). Run FNV-1a 64 (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`) over the bytes of each item written `name=value` followed by one `0x0A` byte, in that order. The result is the data hash. `bitriot_headless version --items` prints the items, to see what differs between two builds that disagree.

**A tool in another language cannot recompute the data hash from the raw files** without re-implementing `DataReader`'s grammar (names and values separated by blanks, `#` comment lines, a value that starts with a letter is a string, one that starts with a digit must be all digits, the first of a duplicated name wins). It can read the record's hash and compare it with `bitriot_headless version`, and, to find what differs, with the lines of `version --items`.

**Eager loading.** `Simulation::start()` and `Simulation::dataHash()` parse both files at once. A file that cannot be read or parsed throws `std::runtime_error` (from `start()` before anything of the running round is touched) instead of every constant silently answering `1`, which is what `DataReader`'s lazy lookup does for a missing key. A key that is missing from a file that does load still reads as `1` in the entities that ask for it; the data hash differs from a peer that has the key, which is how netplay finds out.

## The build id

`Simulation::buildId()` / `bitriotBuildId()` (`src/BuildId.h`, generated by `cmake/GenerateBuildId.cmake`): a 64-bit hash of the simulation's **source**, made at build time.

- **Inputs:** the files of the `bitriot_sim` target, plus every project header they include, directly or not (an `#include` resolves next to the including file, then under `src/`, then under the repository root). The file list is written to `<build>/generated/BuildIdValue.inputs.txt`.
- **Algorithm:** for each file in order of its repository-relative path, append the path, a newline, the file's text **with every carriage return removed**, a newline, and the line `--`. MD5 of that stream. The id is the first 8 bytes as a big-endian number (its 16 hex digits are the first 16 hex digits of the MD5).
- **What it means:** two builds have the same id exactly when the simulation code is the same text, whatever the compiler, flags, optimisation level or operating system. It does not say the two *behave* the same (the cross-platform gate, brief 05b, does). Core-only files (rendering, input, menus, the headless CLI) do not move it.
- **When it is made:** a custom target (`bitriot_build_id`) runs on every build and rewrites `BuildIdValue.cpp` only if the id changed, so editing a simulation file always changes it, and an unchanged simulation does not recompile.
- **Where it is used:** in every record's header; `bitriot_headless replay` notes (on stderr) when a record was made by another build, and goes on. Netplay's `HELLO` (07) compares it and refuses a mismatch. The golden-record test does **not** compare it (the golden file is allowed to outlive the build that wrote it; only the data hash is enforced).

## Checkpoints and what a replay can tell you

`replayRecord` (`src/MatchReplay.*`) starts the simulation from the record's config and steps it through the ticks, comparing `stateHash()` with each checkpoint, tick 0 included. The outcomes:

| Status | Meaning | `bitriot_headless replay` exit |
|---|---|---|
| `OK` | every checkpoint agreed and the round ended (or not) as recorded | 0 |
| `CHECKPOINT_MISMATCH` | the first checkpoint that differs is named: **tick *T*, the last agreeing checkpoint was tick *P***. The divergence is in ticks *P*+1 to *T*; with a checkpoint every tick (the golden record) that is exactly one tick | 1 |
| `SIM_ERROR` | the simulation refused tick *T* (an event for a slot the round lacks, an unknown kind): the file is broken, which is not a determinism failure | 2 |
| `ROUND_END_MISMATCH` | every checkpoint agreed but the round's end differs from the record's `ROUND_OVER` flag | 1 |
| `DATA_HASH_MISMATCH` | made on other game data; nothing is played | 3 |
| `HASH_VERSION_MISMATCH`, `BAD_CONFIG`, `DATA_ERROR` | not replayable here | 2 |

To find the exact tick in a record with sparse checkpoints, re-record with `--checkpoint-every 1` (`run --script <record> --checkpoint-every 1`), or `dump` the two runs at the window's ticks and diff them.

## `bitriot_headless`

A CMake target that links `bitriot_sim` only: no window, no audio, no bitmaps, no SDL. It builds with `-DBITRIOT_BUILD_GAME=OFF`. It reads `data/` from the working directory and nothing from `assets/`.

```
bitriot_headless replay <file> [--hashes] [--ignore-data-hash]
bitriot_headless run --out <file> [--config SPEC] [--seed N] [--script FILE]
                     [--max-ticks N] [--checkpoint-every K] [--no-rle]
bitriot_headless dump <file> [--tick N]
bitriot_headless version [--items]
bitriot_headless help
```

- **`replay`** verifies the record as above. It prints the record's header and config, then `replay: OK, <ticks> ticks, <checkpoints> checkpoints verified, final hash 0x...` or `replay: MISMATCH: ...`. `--hashes` also prints `hash <tick> 0x<hash>` for every tick (tick 0 first), the stream 05b compares across compilers and platforms.
- **`run`** plays a fresh round (AI players, plus the inputs and events of the script, if any) and writes a record. The script is a record file; with no `--config` and no `--seed` its config and seed are used. `--seed` is required otherwise (`0x` hex or decimal). The round ends when it is over (the countdown included) or after `--max-ticks` (default 100000, at most 1,048,576). `--checkpoint-every` defaults to 30; `--no-rle` writes the plain body. This is also how a golden record is regenerated from its own inputs.
- **`dump`** replays to tick *N* (default: the last) and prints every hashed field of every entity, the map and the tile lists, in an order that is the same on every machine. Diff two dumps to find what differs. A checkpoint mismatch before tick *N* is reported on stderr (exit 1) after the dump. Slots are numbered from 0 here, as everywhere.
- **`version`** prints the build id, the data hash and the two format versions. `--items` adds the data hash's items.

**`SPEC`** (`--config`) is comma-separated `key=value` pairs, or `@file` holding them (one or more per line, `#` comments):

| Key | Meaning |
|---|---|
| `players=N` | number of players, 2-6 (default 2) |
| `map=WxH` | map size in tiles (default 17x13) |
| `blocks=P` | block percentage (default 55) |
| `spawner=on\|off` | the NPC spawner (default on) |
| `teams=a:b:c` | each slot's team (default: every slot its own) |
| `humans=0:1` | the slots that start as humans, driven by the script (default none: all AI) |

Every number is range-checked on its full value before it is used: players 2-6, each map side 4-255, blocks 0-100, each team 0-255, each human slot 0-5 (and below `players`); anything else exits 2 at once.

Slots are 0-based. The config is checked loosely (`MatchConfig::runnableProblem`, so small test maps work), not against the menu limits.

**Exit codes:** `0` success, `1` the replay did not match (or a `dump` found a mismatch), `2` bad command line, an unreadable or malformed file, or a record the simulation refuses to play (`SIM_ERROR`, `BAD_CONFIG`), `3` the record was made on different game data.

**A record with no checkpoints** replays and exits 0 (there is nothing to disagree with), but verifies nothing: stdout says `0 checkpoints verified` and stderr carries a warning. A gate that needs verification should check that count.

**stdout is the result, and is the same on every build and machine for the same input**: no paths, times or addresses. Anything that depends on the build (the "made by another build" note, errors) goes to stderr. Two processes replaying the same record print the same bytes, which `HeadlessCliTest` checks by starting two at once.

**For later briefs.** 05b runs `replay --hashes` and `version` under each compiler, flag set, architecture and wasm, and diffs stdout. 07's multi-process tests extend this binary with `--host` / `--join` and keep the command names. 13's joiner fast-forwards by `replayRecord` on the host's record, with `MatchReplay.h` as the API.

## `--record` in the game

`BitRiot --record <dir>` writes one record per round into `<dir>` (created at start-up; the game exits with an error if it cannot be). The file is written when the round ends or is quit with ESC, named `round-<UTC date>-<UTC time the round started>-seed<seed>.brmr` (with `-2`, `-3` ... if that exists), with a checkpoint every 30 ticks and RLE on. Closing the window mid-round keeps what was played. A round that cannot be recorded or written is logged and the game goes on: recording only reads the simulation (the `TickInputs` it was handed and a state hash), and cannot change a round. The command line is parsed with `getopt_long`: `-f`/`--fullscreen`, `-b`/`--big-map`, `-h`/`--help`, `--record <dir>`; an unknown option or a stray argument prints the usage and exits with 2.

## Versioning

- **Format version** (`formatVersion`, 1): bump it for any change to the layout above. A reader refuses a version it does not know.
- **State hash version** (`Simulation::STATE_HASH_VERSION`, 2): a record's checkpoints are only comparable with a build of the same version.
- **Build id / data hash:** change with the code and the data, with no number to bump.
- The `ControlEvent` kind values (0, 1, 2) and the `PlayerInput` bit assignments are part of the format: changing them breaks every stored record.
