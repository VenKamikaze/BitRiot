# Netplay and WASM: work briefs

Bit Riot has local (couch) multiplayer only. This folder plans:

- online multiplayer using **deterministic lockstep**, where every peer runs the full simulation and only player inputs cross the network;
- a **WebAssembly/Emscripten build** that runs in a browser;
- **vnet**, a small generic "virtual UDP" library with a relay server. The browser build uses it, and other games (Odamex, jfSW) could later.

Each `NN-*.md` brief is one unit of work, sized for a single implement → review → merge cycle. A brief is written so that a fresh session with no other context can pick it up. Start with this file, then read the brief, then [`research/code-facts.md`](research/code-facts.md), which records what the code actually does, with file:line refs.

## Order and dependencies

| # | Brief | Repo | Depends on | Status |
|---|---|---|---|---|
| 01 | [Round lifecycle hygiene](01-round-lifecycle-hygiene.md) | BitRiot | — | todo |
| 02 | [Seeded simulation RNG and float guards](02-sim-rng-and-float-guards.md) | BitRiot | 01 | todo |
| 03 | [Headless `Simulation` and state hash](03-headless-simulation.md) | BitRiot | 01, 02 | todo |
| 04 | [Split input sampling from application](04-input-split.md) | BitRiot | 03 | todo |
| 05 | [Match record, replay and determinism tests](05-replay-and-determinism.md) | BitRiot | 02, 03, 04 | todo |
| 06 | [vnet core: API, loopback and native UDP](06-vnet-core.md) | vnet | — | todo |
| 07 | [Native lockstep netplay](07-lockstep-netplay.md) | BitRiot | 05, 06 | todo |
| 08 | [Lobby UI](08-lobby-ui.md) | BitRiot | 07 | todo |
| 09 | [WASM port (local play)](09-wasm-port.md) | BitRiot | 03 (recommended) | todo |
| 10 | [vnet relay and WebSocket transport](10-vnet-relay-websocket.md) | vnet | 06 | todo |
| 11 | [vnet WebTransport datagrams](11-vnet-webtransport.md) | vnet | 10 | todo |
| 12 | [Browser netplay](12-browser-netplay.md) | BitRiot | 07, 09, 10 | todo |
| 13 | [Disconnects and hot-join](13-disconnect-and-hot-join.md) | BitRiot | 07 (12 for browser) | todo |

Two tracks can run in parallel. **01 → 05** (determinism groundwork) and **06** (vnet core) share no code. **09** (WASM, local play only) can also start once 03 is in. Everything else follows the table.

```
01 ─► 02 ─► 03 ─► 04 ─► 05 ─┐
                │           ├─► 07 ─► 08
                │     06 ───┤     │
                │      │    │     └──────► 13
                └► 09  └► 10 ─► 11
                    │     │
                    └─────┴──► 12 (needs 07, 09, 10)
```

**In-flight work to merge first.** `fix/ai-seek-energy` changes the bot AI in `src/PlayerCharacterEntity.cpp`, where a lot of `rand()` calls live. Brief 02 must start from a `claude` that already contains it.

## Settled decisions

Don't reopen these in a brief. If one turns out to be wrong, stop and raise it with the user.

- **D1. Deterministic lockstep, input-only.** There are no state snapshots. Every peer simulates everything. AI players cost no bandwidth because they run identically on every peer.
- **D2. Star topology.** One host (or the relay) collects inputs and rebroadcasts them. It is not peer-to-peer mesh.
- **D3. vnet is its own repository**, `VenKamikaze/vnet`, holding the C library, its backends and the relay. **The user has to create it**, because the session's GitHub integration cannot create repositories. Briefs 06, 10 and 11 stay in this folder until it exists; move them into the vnet repo's `doco/` when 06 starts.
- **D4. The relay is written in Go.** That means `quic-go/webtransport-go` plus a WebSocket library in one static binary. Python is fine for test tools such as fake clients and loss simulators. Don't use Java or Spring for the relay.
- **D5. No SDL_net.** Native netplay goes through vnet's native UDP backend from day one. vnet also has an in-process **loopback backend** with simulated delay and loss, for tests.
- **D6. Keep floats, guard them, and prove it.** The sim's float maths is add, subtract, divide and compare only, with no multiply-adds (code-facts §4). That is bit-identical across x86-64, aarch64 and wasm, with these build guards:
  - no `-ffast-math`
  - `-ffp-contract=off`
  - SSE2 on any 32-bit x86 build

  Brief 05 adds a cross-platform checksum gate, and brief 12 extends it to native-vs-wasm. **Fixed-point conversion is a contingency**, done only if that gate fails. It is about 12 files and slightly changes how play feels, because velocities like 110/400 aren't exactly representable. *(The user first leaned towards fixed-point; this decision was revised after the code check. Confirm with the user if in doubt.)*
- **D7. The protocol is loss-tolerant and transport-agnostic.** Each input packet carries every input the receiver hasn't acknowledged yet, so it works unchanged over lossy UDP and over reliable WebSocket.
- **D8. Input delay defaults to 3 ticks** (~100 ms at 33 ms/tick) and is configurable. When a tick's inputs are missing, the sim stalls rather than guessing. There is no rollback.
- **D9. Browser transport:** WebSocket first, then WebTransport datagrams, keeping WebSocket as the fallback.

## Open questions for the user

The relevant brief says what to assume until each is answered.

1. **GameOverTimer length.** The intended countdown is 2 s, but a bug makes it 5 s (code-facts §3). Brief 01 keeps the 5 s players are used to unless told otherwise.
2. **D6.** Is the user happy with floats-plus-gate rather than fixed-point up front?
3. **vnet repo visibility** (private or public) and licence.
4. **Online player cap.** Keep the local maximum of 6? Brief 07 assumes 6.

## How to work a brief

These are the user's conventions for this repo.

- **Branches.** `claude` is the base branch. Create `netplay/NN-short-name` from it, and merge back with `--no-ff` once reviewed, then push both. Never commit directly to `claude`.
- **Process** (the user's preferred one):
  1. A coordinator session spawns an implementer (Sonnet) with the brief.
  2. A reviewer (Opus) reviews the branch read-only.
  3. The implementer does a fix round.
  4. If the fixes were non-trivial, there is a re-review.
  5. The coordinator verifies and merges.
- **Environment.** A fresh cloud container needs [`scripts/cloud-setup.sh`](../../scripts/cloud-setup.sh): SDL2, GTest, and RmlUi 4.3 built into `/usr/local`. Then build and test:
  ```bash
  cmake -S . -B build && cmake --build build -j8 && ./build/tests/bitriot_tests
  ```
- **Tests.**
  - Read [`tests/README.md`](../../tests/README.md) first.
  - Every brief adds tests that **fail without the change**; prove it by reverting the source change.
  - Never skip, loosen or quarantine a test.
  - For anything touching the sim, run a seed sweep (for `s` in 1..200, `BITRIOT_TEST_SEED=$s`) before and after, and report both numbers.
- **When a brief is done:**
  - Set its Status in the table above.
  - Add a short **Outcome** section to the brief: what landed, any deviations, and follow-ups found. The next brief's author reads it.
- **Scope.** If a brief's assumption about the code turns out to be wrong, fix the brief (and code-facts) in the same branch. If the scope has to grow a lot, stop and ask.

## Beyond Bit Riot (not briefed yet)

Once vnet's API has settled after briefs 06, 10 and 12, it is meant to serve other games' existing net drivers:

- **jfSW**: Build-engine `mmulti.c` lockstep. Port with the classic software renderer.
- **Odamex**: a client/server Doom port with hot-join. It wants unreliable datagrams, i.e. WebTransport (brief 11).
- **The relay's optional UDP gateway**, so browser clients can join native servers. This must use an allowlist of destinations, or it becomes an open UDP proxy.

Write briefs for these after brief 12 lands, not before.
