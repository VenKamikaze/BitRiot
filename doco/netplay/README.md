# Netplay and WASM: work briefs

Bit Riot has local (couch) multiplayer only. This folder plans:

- online multiplayer using **deterministic lockstep**, where every peer runs the full simulation and only player inputs cross the network;
- a **WebAssembly/Emscripten build** that runs in a browser;
- **vnet**, a small generic "virtual UDP" library with a relay server. The browser build uses it, and other games (Odamex, jfSW) could later.

Each brief (`NN-*.md`) is one unit of work, sized for a single implement → review → merge cycle, and written so that a fresh session with no other context can pick it up.

**Starting a fresh session on this work?** Point it at [`PROMPT.md`](PROMPT.md).

**Reading order:** this file, then the brief, then [`research/code-facts.md`](research/code-facts.md), which records what the code actually does with file:line refs. Then read the **Outcome** sections of every brief yours depends on: they record what actually landed.

## Order and dependencies

| # | Brief | Repo | Depends on | Status |
|---|---|---|---|---|
| 01 | [Round lifecycle hygiene](01-round-lifecycle-hygiene.md) | BitRiot | — | done |
| 02 | [Seeded simulation RNG](02-sim-rng.md) | BitRiot | 01 | done |
| 02b | [Fixed-point simulation maths](02b-fixed-point-sim.md) | BitRiot | 01, 02 | done (play-test pending) |
| 03a | [`Simulation`, `MatchConfig` and state hash](03a-simulation-and-state-hash.md) | BitRiot | 01, 02, 02b | done |
| 03b | [Surface-free sim library and frame loop](03b-surface-free-sim-and-frame-loop.md) | BitRiot | 03a | todo |
| 04a | [Input sampling and `step(inputs)`](04a-input-sampling.md) | BitRiot | 03b | todo |
| 04b | [Control events, rumble and UI plumbing](04b-control-events.md) | BitRiot | 04a | todo |
| 05 | [Match record, replay and `bitriot_headless`](05-replay-and-determinism.md) | BitRiot | 03b, 04b | todo |
| 05b | [Cross-platform determinism gate](05b-cross-platform-gate.md) | BitRiot | 05 | todo |
| 06 | [vnet core: API, loopback and native UDP](https://github.com/VenKamikaze/vnet/blob/claude/doco/06-vnet-core.md) | vnet | — | tracked in vnet |
| 07 | [Native lockstep netplay](07-lockstep-netplay.md) | BitRiot (+vnet) | 05b, 06 | todo |
| 08 | [Lobby UI](08-lobby-ui.md) | BitRiot | 07 | todo |
| 09 | [WASM port (local play)](09-wasm-port.md) | BitRiot | 03b | todo |
| 10 | [vnet relay and WebSocket transport](https://github.com/VenKamikaze/vnet/blob/claude/doco/10-vnet-relay-websocket.md) | vnet | 06 | tracked in vnet |
| 11 | [vnet WebTransport datagrams](https://github.com/VenKamikaze/vnet/blob/claude/doco/11-vnet-webtransport.md) | vnet | 10 | tracked in vnet |
| 12 | [Browser netplay](12-browser-netplay.md) | BitRiot (+vnet) | 07, 08, 09, 10 | todo |
| 13 | [Disconnects and hot-join](13-disconnect-and-hot-join.md) | BitRiot | 05, 07, 08 (12 for browser) | todo |

`Qn` means open question *n* below. The user must answer it before that brief starts, because an implementer subagent cannot ask the user.

**Parallel tracks:**

- **Determinism:** 01 → 02 → 02b → 03a → … → 05b.
- **vnet core:** 06 shares no code with the BitRiot briefs.
- **WASM:** 09 can start once 03b is in.

**Sim/render split.** 03b owns the frame-loop refactor: the shared `frame()`, the fixed-tick accumulator, and render without stepping. 04a, 07 and 09 build on it. Don't restructure `main.cpp` or `GameEngine::runEngine` elsewhere.

**Merged prerequisite.** `fix/ai-seek-energy` (merged in 7d6cf6a) changed the bot AI in `src/PlayerCharacterEntity.cpp`, where many `rand()` calls live. Brief 02 must re-find those call sites rather than trust the line numbers in code-facts §2. It also added the energy-seeking flood fill and a leak-free A*; see `tests/AISeekEnergyTest.cpp`.

## Settled decisions

Don't reopen these inside a brief. If one turns out to be wrong, stop and raise it with the user.

- **D1. Deterministic lockstep, input-only.** No state snapshots. Every peer simulates everything, and AI players cost no bandwidth.
- **D2. Star topology.** One peer is the **host**: it collects inputs, decides each tick's final inputs, and rebroadcasts them. The relay (10) is only a router and has no game logic.
- **D3. vnet lives in its own repository**, [`VenKamikaze/vnet`](https://github.com/VenKamikaze/vnet), holding the C library, its backends and the relay.
  - **Licence:** MIT. **Visibility:** public, so fetching it needs no credentials.
  - Briefs 06, 10 and 11 live in that repo's `doco/`, on its `claude` branch. Its `doco/README.md` repeats D3–D5, D7 and D9, and its status table is the one that counts for those briefs.
  - A session working on 06, 10 or 11 needs the vnet repo attached. Briefs 07 and 12 need both repos.
- **D4. The relay is written in Go**, using `quic-go/webtransport-go` plus a WebSocket library, built as one static binary. Python is fine for test tools. Not Java/Spring.
- **D5. No SDL_net.** Native netplay uses vnet's native UDP backend. vnet also has an in-process **loopback backend** with simulated delay and loss, for tests.
- **D6. Fixed-point sim maths, then prove it.** *(The user's answer to Q2, Oct 2026. Brief 02b landed it.)*
  - The sim uses no floating point. Offsets, velocities, the A\* open-list keys and the rocket's target are integers: `fixed_t`, Q16.16, in `src/Fixed.h` (brief **02b**, done; it carries a play-test because play feel shifts very slightly).
  - Floats remain only outside the sim (rendering and HUD, rumble, window scaling, RmlUi, audio). `FixedPointSourceGuardTest` fails if a `float`, `double`, floating literal or libm call appears in any `src/` file that is not on its allowlist, so they cannot creep back. (It is a text scan: it cannot see an `auto` carrying a double or an implicit int-to-float conversion, so brief 03b adds an object-level check on the `bitriot_sim` objects.)
  - Brief **05b** still runs **before** netplay is built, as a confirmation gate for other sources of divergence (UB, libstdc++ vs libc++, evaluation order):
    - GCC vs Clang;
    - an aggressive-flags build;
    - optionally aarch64 under qemu;
    - wasm under Node.
  - A 05b failure is a bug to find and fix, not a reason to revisit D6. If it can't be fixed within 05b's scope, stop and ask the user.
  - *(History: the plan first kept floats, guarded by `-ffp-contract=off` and no `-ffast-math`, with fixed-point only as a contingency. The sim's float maths is only add, subtract, divide and compare, so that would probably have worked, but the user preferred integers by construction.)*
- **D7. A loss-tolerant, transport-agnostic protocol.** Every input packet repeats all unacknowledged inputs, so it works unchanged over lossy UDP and over reliable WebSocket.
- **D8. Input delay with no rollback.** A local input sampled at tick T applies at T+D. If a tick's inputs are missing, the sim stalls rather than guessing.
  - D is chosen by the host from measured round-trip times at match start (07). D=3 is the default for tests and loopback only.
- **D9. Browser transport:** WebSocket first, then WebTransport datagrams, keeping WebSocket as the fallback.

## Open questions for the user (prerequisites)

1. ~~(Before 01) GameOverTimer length~~ **Answered: 2 s / 60 ticks** (at 33 ms/tick), as originally intended. The old 5 s was an accident of a virtual call in a base constructor (code-facts §3). Brief 01 makes it one named constant.
2. ~~(Before 02) D6~~ **Answered:** fixed-point up front (brief 02b), not floats-plus-gate.
3. ~~vnet repo~~ **Answered:** MIT licence, public repo.
4. ~~(Before 07) Online player cap~~ **Answered:** keep the local maximum of 6.

## How to work a brief

These are the user's conventions for this repo.

**Branches**

- `claude` is the base branch. Create `netplay/NN-short-name` from it.
- Merge back with `--no-ff` once reviewed, then push both.
- Never commit directly to `claude`.

**Process.** The user's preferred flow:

1. A coordinator session gives the brief to a Sonnet implementer.
2. An Opus reviewer reviews the change read-only, building in a throwaway worktree.
3. The implementer does a fix round.
4. If the fixes were non-trivial, the reviewer re-reviews.
5. The coordinator verifies and merges.

**Play-tests.** Some acceptance criteria need the user to play the game; the cloud container has no display.

- The coordinator **may merge** with a play-test pending if everything automated passes.
- Mark the status as `done (play-test pending)`.
- Give the user exact commands or steps.
- Record the result in the Outcome when it comes back.

**Environment**

- A fresh cloud container needs [`scripts/cloud-setup.sh`](../../scripts/cloud-setup.sh): SDL2 and GTest from apt, and RmlUi 4.3 built into `/usr/local`.
- Build and test:
  ```bash
  cmake -S . -B build && cmake --build build -j8 && ./build/tests/bitriot_tests
  ```

**Network limits in the cloud container.** Checked Oct 2026, under the environment's *limited* network policy.

The user has since switched the environment to **full network access**, which applies to sessions started after the change. At the start of a brief, check whether archive downloads work, for example:

```bash
curl -sS -o /dev/null -w "%{http_code}\n" -L https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.30.0.zip
```

`200` means the limits below don't apply. Under the limited policy:

- **Works:**
  - `git clone` from github.com
  - storage.googleapis.com (the emsdk binaries)
  - proxy.golang.org and sum.golang.org (Go modules)
  - npm
  - apt
  - the preinstalled Playwright + Chromium (`require('playwright')`; never run `playwright install`)
- **Blocked:** github.com **archive downloads** (`…/archive/*.zip|tar.gz`, return 403). This affects:
  - **Emscripten ports** (`-sUSE_SDL=2` etc.). Git-clone each port's source at the tag Emscripten expects and point `EMCC_LOCAL_PORTS` at the clones.
  - **CMake `FetchContent`.** Use `GIT_REPOSITORY`, never `URL …zip`, or use a system package (e.g. `libgtest-dev`).
- If something else is blocked, stop and tell the user which host is needed. Don't work around a denial.

**Tests**

- Read [`tests/README.md`](../../tests/README.md) first.
- Every brief adds tests that **fail without the change**. Prove it by reverting the source change.
- Never skip, loosen or quarantine a test.
- Anything touching the sim gets a 200-seed sweep before and after (`BITRIOT_TEST_SEED=1..200`), with both numbers reported.

**When a brief is done**

- Set its status in the table above.
- Add an **Outcome** section to the brief covering what landed, deviations, and follow-ups found.

**Scope.** If a brief's assumption about the code turns out to be wrong, fix the brief and code-facts in the same branch. If the scope needs to grow a lot, stop and ask.

## Beyond Bit Riot (not briefed yet)

Once vnet's API has settled (after 06, 10 and 12), it is meant to serve other games' existing net drivers:

- **jfSW**: Build-engine `mmulti.c` lockstep; use the classic software renderer.
- **Odamex**: a client/server Doom port with hot-join. It wants unreliable datagrams, i.e. 11.
- **An optional relay UDP gateway** so browser clients can join native servers. This needs an allowlist of destinations, or it becomes an open proxy.

Brief these after 12 lands.
