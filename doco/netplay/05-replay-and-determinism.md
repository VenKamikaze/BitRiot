# 05 — Match record, replay and determinism tests

- **Repo:** BitRiot
- **Branch:** `netplay/05-replay`
- **Depends on:** 02, 03, 04
- **Status:** todo

## Goal

A match is fully described by `MatchConfig` + data-file hash + the `TickInputs` sequence. Record that in a small file, replay it headlessly, and prove the replay reproduces every tick's `stateHash()`. This is the core test tool for everything networked: lockstep netplay is the same replay fed live.

## Read first

- [`README.md`](README.md): D6, the float gate.
- Briefs 02, 03 and 04, including their Outcome sections. You need the PRNG, `MatchConfig`, `Simulation`, `stateHash()` and `TickInputs` as they actually landed.
- [`research/code-facts.md`](research/code-facts.md) §7, the data files. A missing key silently returns 1, so the data must be hashed.

## In scope

1. **The `MatchRecord` format:**
   - A versioned binary format, little-endian with fixed-width ints, so a future JS or Go reader can parse it.
   - Contents:
     - a header (magic, format version, build/protocol version, `MatchConfig`)
     - a **data hash** (see 2)
     - then per tick: the `TickInputs`
     - optionally, a `stateHash` every K ticks, for example every 30, as checkpoints
   - Keep it compact. Run-length encode runs of identical inputs or use deltas if easy; it is not required.
   - Document the format in `doco/netplay/match-record-format.md`.
2. **Data hash:** a stable hash of the sim-relevant data the game loaded.
   - At minimum `data/entity.txt` and `data/player.txt`, normalised by key and value, or hashed as raw bytes; pick one and document it.
   - Also any sim constants that aren't in data files. List what you include.
   - Expose it, for example as `Simulation::dataHash()`. Netplay (07) sends it in its handshake.
3. **Recorder:** local play can record a match to a file, via a command-line flag (for example `--record <file>`) or a setting. Recording must not change gameplay.
4. **Replayer and headless tool:** a `bitriot_headless` executable (CMake target) that:
   - replays a record with no window and no audio, using the null sound backend and whatever bitmap handling brief 03 settled on;
   - verifies the checkpoint hashes and reports the first mismatching tick;
   - can also run a fresh match from a config and seed, with AI-only or scripted inputs, and write a record.

   This binary is reused by 07 (multi-process peers) and 13 (hot-join fast-forward), so keep its CLI clean.
5. **Desync diagnostics:**
   - A `--dump-tick N` (or similar) option that writes a readable per-entity state listing for tick N.
   - Two dumps from diverging runs can then be diffed. Netplay relies on this when a checksum mismatch is reported.

## Out of scope

- Networking.
- A UI for browsing or watching replays (playback with rendering is a nice-to-have; skip it unless trivial).
- Native vs wasm comparison (brief 12 does that once wasm exists).

## Tests

- **Round-trip:** run a seeded AI-only match with some scripted human inputs, record it, replay it, and get an identical hash on every tick. Also check the record serialises and deserialises byte-identically.
- **Golden record:** commit a small record file under `tests/data/` (a few hundred ticks, a real map). Replaying it must match its checkpoints.
  - This is the long-term regression guard: any change to sim behaviour breaks it. That is intended.
  - Document in `tests/README.md` how to regenerate it **deliberately**, when a gameplay change is intended, and that regeneration must be called out in the commit message.
- **Float gate (D6):** the golden-record replay is the cross-platform gate. Run it in at least the Debug (-O0) and Release (-O2) builds and confirm both match the same golden hashes.
  - If an aarch64 environment is available, run it there too. If not, say so in the Outcome; brief 12 adds wasm.
  - If -O0 and -O2 disagree, stop and report. That would trigger the fixed-point contingency.
- **Mismatch detection:** corrupt one input in a record, and the replayer reports the first diverging tick.
- **Multi-process determinism:** run the same record in two separate `bitriot_headless` processes and get identical output. This is cheap, and it exercises the singleton-free start-up path.

## Acceptance criteria

- Record → replay is bit-exact on every tick, in Debug and Release.
- A golden record is in the suite, with documented regeneration.
- `bitriot_headless` exists, with record, replay, verify and dump.
- The format doc is written.
- `tests/README.md` is updated.

## Outcome

*(fill in when done)*
