# 07 — Native lockstep netplay

- **Repo:** BitRiot
- **Branch:** `netplay/07-lockstep`
- **Depends on:** 05 (replay, `bitriot_headless`, data hash) and 06 (vnet with native UDP, loopback and impairment)
- **Status:** todo

## Goal

Two to six players on different machines play a Bit Riot match over the network. Each machine may also have local couch players. Every peer runs the same `Simulation`, and only `TickInputs` cross the network. Desyncs are detected within a second and produce diagnostics.

This brief is native only and driven from the command line. The lobby UI is brief 08; browsers are brief 12.

## Read first

- [`README.md`](README.md): decisions D1, D2, D5, D7 and D8.
- The Outcome sections of briefs 02–06. Use the APIs as they actually landed.
- [`research/code-facts.md`](research/code-facts.md) §1 (ESC/quit, the tick on which a round ends), §6 (join/takeover becomes events), §7 (settings to share) and §11.

## Design (settled; refine the details)

- **Topology:** one peer is the **host** (D2). The host is authoritative for:
  - the final `TickInputs` of every tick (it merges every peer's slots plus control events);
  - match settings, seed and slot assignment;
  - round start, round end, and the next round.
- **Input delay** (D8): a local input sampled at tick T is scheduled for tick T+D, with D=3 by default and configurable. A peer only steps tick T once it holds the host's confirmed `TickInputs` for T. Otherwise it **stalls** and shows "waiting for <player>".
  - A host in a hurry must not run ahead of its slowest peer by more than D. Define how the host paces.
- **Catch-up:** a peer that has fallen behind, i.e. holds more than D+2 confirmed ticks it hasn't stepped, may step up to 2 extra ticks per frame until it is level. This keeps clock drift between machines from building up.
- **Loss tolerance** (D7):
  - Every `INPUT` packet (peer → host) repeats all of that peer's not-yet-acknowledged ticks.
  - Every `TICKS` packet (host → peer) repeats all confirmed ticks the peer hasn't acknowledged.
  - Each side piggybacks an ack of the highest contiguous tick it has received.
  - Cap packets at `VNET_MAX_DATAGRAM`. If the backlog is bigger, send the oldest first.
- **Messages:** version every message, with a protocol version in `HELLO`.

  | Message | Content |
  |---|---|
  | `HELLO` | protocol version, build id, data hash (05), player name, number of local players wanted |
  | `WELCOME` | assigned slots |
  | `REJECT` | reason: version, data mismatch, full, … |
  | `LOBBY` | host → all: slot table (host/peer/local/AI, team), settings |
  | `START` | `MatchConfig` (with seed), first tick, input delay |
  | `INPUT` | peer → host: ack, first tick, per tick: that peer's slots' `PlayerInput`s + its `ControlEvent`s |
  | `TICKS` | host → peers: ack, first tick, per tick: full `TickInputs` |
  | `CHECKSUM` | peer → host: tick, `stateHash()`, every 30 ticks |
  | `DESYNC` | host → all: tick. Everyone dumps that tick's state (05's dump) and the match stops with a clear message. |
  | `PING`/`PONG` | RTT for display |
  | `LEAVE` | explicit quit (ESC) |

- **Leaving:** a peer that leaves (`LEAVE`) or times out (no packets for, say, 10 s) has its slots returned to AI. The host schedules a `RETURN_TO_AI` `ControlEvent` for those slots on a future confirmed tick. The match continues. Smarter disconnect handling and reconnection are brief 13.
- **Rounds:**
  - The round ends on the same tick everywhere, because the tick-based GameOverTimer (01) is part of the sim.
  - Every peer shows the scoreboard.
  - The host sends the next `START`, with a new seed. Peers can't advance the round themselves.
- **Local players:** each peer owns some slots. Its local `InputSampler` (04) fills only those slots; everything else comes from the host.
- **Mid-match takeover of AI slots** by a local player is a `ControlEvent` request sent to the host, which schedules it. Keep the request simple; the full hot-join for remote newcomers is brief 13.
- **Module layout:** put the code in `src/net/`.
  - Keep `LockstepSession` (protocol, buffering, acks, pacing) **free of SDL and of `Simulation`**. It exchanges `TickInputs` and hashes through a small interface, so it can be unit-tested with fake peers on vnet loopback in a single process. This matters because the singletons allow only one `Simulation` per process.
- **vnet dependency:** add it as a submodule under `ext/vnet` (consistent with RmlUi) or through `FetchContent`, and justify the choice.
  - Update `scripts/cloud-setup.sh` if anything new is needed.
  - Put it behind a `BITRIOT_NETPLAY` CMake option, ON by default.
- **CLI, until brief 08 adds UI:**
  - `--host [port]`, `--join host:port`, `--name <n>` and `--local-players N`.
  - Both peers then go straight to the lobby, the host starts with a key press, and existing local controls apply.

## Tests

- **`LockstepSession` unit tests,** with N fake peers in one process over vnet loopback with impairment:
  - Every peer receives every tick's identical `TickInputs`, in order.
  - This holds under 0%, 5% and 20% loss, duplication and reordering, at an 80 ms latency.
  - Stall when the host withholds a tick.
  - Catch-up behaviour.
  - Leave and timeout produce a scheduled `RETURN_TO_AI`.
  - Version mismatch and data-hash mismatch are rejected.
- **Multi-process integration:**
  - A test that starts 2–3 `bitriot_headless` processes (extend 05's tool with `--host`/`--join`) over native UDP on 127.0.0.1 with impairment.
  - They play an AI-only match, with some scripted human inputs, to the end.
  - Assert that all processes report identical final and checkpoint hashes and the same winner.
  - Run it under ctest with a timeout. A `--gtest_filter` that excludes it from the fast default suite is fine, provided the README documents it and the coordinator runs it.
- **Desync detection:** deliberately perturb one peer, for example through a test-only flag that changes one entity's health. The host reports `DESYNC` at the first checkpoint after the perturbation, and both peers write dumps.
- Prove the new tests fail without the code where that makes sense (for example, the session tests with redundancy disabled under loss).

## Acceptance criteria

- **Two real game windows on one machine:** `--host` and `--join 127.0.0.1:port` play a full match. There is no window access in the cloud container, so ask the user to play-test this, and give them exact commands.
- **The multi-process headless test passes** under impairment.
- **Desyncs:** they are detected and reported with dumps.
- **Disconnects:** a lost peer's slots become AI, and the match continues.
- **No regressions in local play:** `--host`/`--join` absent means exactly today's behaviour.
- **Protocol doc:** `doco/netplay/protocol.md` documents every message's fields and versioning.

## Outcome

*(fill in when done)*
