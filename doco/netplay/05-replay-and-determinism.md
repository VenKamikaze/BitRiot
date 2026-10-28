# 05 — Match record, replay and `bitriot_headless`

- **Repo:** BitRiot
- **Branch:** `netplay/05-replay`
- **Depends on:** 03b, 04b
- **Status:** todo

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

*(fill in when done)*
