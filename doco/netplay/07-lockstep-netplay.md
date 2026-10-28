# 07 — Native lockstep netplay

- **Repo:** BitRiot. The vnet repo must also be attached to the session: it is consumed here.
- **Branch:** `netplay/07-lockstep`
- **Depends on:**
  - 05b: D6 confirmed; `bitriot_headless`, the record format, the data hash and the build id (all from 05)
  - 06: vnet with native UDP, loopback and impairment
  - open question Q4 (player cap)
- **Status:** todo

## Goal

Two to six players on different machines play a Bit Riot match over the network. Each machine may also have local couch players.

- Every peer runs the same `Simulation`, and only `TickInputs` cross the network.
- Desyncs are detected within a second and produce usable diagnostics.

This brief is native-only and driven from the command line. The lobby UI is brief 08 and browsers are brief 12.

## Read first

- [`README.md`](README.md): decisions D1, D2, D5, D7 and D8.
- The Outcome sections of briefs 03a–06. Use the APIs as they actually landed, especially `frame()`/`canStep()` (03b), the local slot mask (04a) and `ControlEvent`s (04b).
- [`research/code-facts.md`](research/code-facts.md): §7 (settings to share) and §12 (scoreboard "Play Again", `setPointers`, `getopt`).

## Design (settled; refine the details)

**Host authority (D2).** The host is authoritative for:

- the final `TickInputs` of every tick, merging every peer's slots and control events;
- the match settings, seed and slot assignment;
- round start, round end and the next round.

**Input delay (D8).** A local input sampled at tick T is scheduled for T+D. A peer steps T only when it holds the host's confirmed `TickInputs` for T. Otherwise `canStep()` returns false, which stalls the peer, and it shows "waiting for <name>". Each `TICKS` packet tells peers who the host is still waiting on.

- **The host chooses D** from lobby `PING` statistics:
  - a peer's input needs a full round trip to come back confirmed, plus up to one frame of sampling and send phase;
  - so take D = `ceil((max RTT + jitter) / 33 ms) + 1`, clamped to 2..8;
  - send it in `START`, and log it;
  - over a relay (brief 12) there are two relay hops, so expect a higher D;
  - D=3 is only the default for tests and loopback.
- **Ticks 0..D−1** have no sampled input. They are implicitly confirmed as empty at `START`.
- **The host doesn't run ahead** of its slowest peer by more than D. Define exactly how it paces.

**Catch-up.** A peer holding more than D+2 confirmed but unstepped ticks may step up to 2 extra ticks per frame (`frame()`'s accumulator from 03b) until it is level.

**Sampling.** A peer samples once per stepped tick (04a). While stalled, local events accumulate into the next sample. **Only local slots are sampled**, using the local slot mask; remote humans must be excluded from the keyboard assignment in `setPointers` (code-facts §12).

**Loss tolerance (D7).**

- Every `INPUT` packet (peer → host) repeats all of that peer's unacknowledged ticks.
- Every `TICKS` packet (host → peer) repeats all confirmed ticks the peer hasn't acknowledged.
- Both piggyback an ack of the highest contiguous tick received.
- Packets are capped at `VNET_MAX_DATAGRAM`, sending the oldest ticks first.

**Messages.** All are versioned, with a protocol version in `HELLO`.

| Message | Content |
|---|---|
| `HELLO` | protocol version, **build id**, **data hash** (05), name, number of local players wanted |
| `WELCOME` | assigned slots, plus a reconnect token (used in 13) |
| `REJECT` | the reason. A mismatched build id is rejected even if the protocol version matches. |
| `LOBBY` | host → all: slot table (host / peer / local / AI, team) and settings |
| `START` | `MatchConfig` including the seed, D, first tick |
| `INPUT` | ack, first tick, then per tick this peer's slots' `PlayerInput`s and `ControlEvent` requests |
| `TICKS` | ack, first tick, then per tick the full `TickInputs`, plus who the host is waiting on |
| `CHECKSUM` | peer → host: tick, `stateHash()`, sent every 30 ticks |
| `DESYNC` | host → all: tick (see below) |
| `PING` / `PONG` | RTT measurement for choosing D, and for display |
| `KEEPALIVE` | sent in every state, including menus |
| `LEAVE` | explicit quit (ESC) |

**Session pumping in every state.**

- The session is polled and sends keepalives in `MENU_RUNNING` too: the scoreboard, between rounds and in the lobby. Otherwise a peer reading the scoreboard times out.
- **On peers, the scoreboard's "Play Again" is disabled or ignored** (`scoreboard.rml` sets `GAME_INIT` locally, code-facts §12). Only the host's `START` begins a round.

**Settings isolation.** Applying `MatchConfig` writes `GameSettings` (03a). A joining peer snapshots its local settings before the match and restores them when it leaves.

**Rounds.**

- The round ends on the same tick everywhere, because the round-end logic is part of the sim (01, 03a).
- Every peer shows the scoreboard.
- The host sends the next `START` with a new seed.

**Leaving and timeouts.**

- When a peer sends `LEAVE` or times out (no packets for ~10 s), its slots return to AI. The host schedules `RETURN_TO_AI` events on a future confirmed tick, and the match continues.
- **Host loss:** there is no host migration. Peers detect the host timing out, end the match with a clear message, and return to the menu.
- Reconnecting is brief 13.

**Local takeover of an AI slot mid-match.** The local join selector produces a `ControlEvent` request. The peer sends it to the host in `INPUT`, and the host schedules it on a confirmed tick.

**Desync diagnostics.**

- By the time `DESYNC` arrives, peers are already past the tick in question, and undefined behaviour might not replay the same way. So **every peer keeps a ring buffer of state dumps for its last K checkpoint ticks**, using brief 05's dump format.
- On `DESYNC`, every peer writes:
  - the dump for that tick;
  - its `MatchRecord` of the round so far.
- The match then stops with a message giving the file paths.

**Code layout.**

- Put the code in `src/net/`.
- **`LockstepSession` stays free of SDL and of `Simulation`.** It handles the protocol, buffering, acks, pacing and the choice of D, and exchanges `TickInputs` and hashes through a small interface. That lets it be unit-tested with N fake peers on vnet loopback in a single process, even though the singletons allow only one `Simulation` per process.

**Dependency.**

- Pull in vnet as a submodule at `ext/vnet` or through `FetchContent` with `GIT_REPOSITORY`. Justify the choice.
- Update `scripts/cloud-setup.sh` as needed. A private vnet repo needs credentials; see Q3.
- Add a `BITRIOT_NETPLAY` CMake option: ON by default, and OFF on MinGW unless brief 06 verified that vnet builds there.

**Command line until brief 08.**

- Options: `--host [port]`, `--join host:port`, `--name <n>`, `--local-players N`. Switch to `getopt_long`.
- Both peers enter a text-mode or minimal lobby, and the host starts the match with a key.

## Tests

**`LockstepSession` unit tests**, with N fake peers on vnet loopback with impairment:

- identical, in-order `TickInputs` on every peer, at 0%, 5% and 20% loss plus duplication and reordering, at 80 ms latency;
- stall and catch-up;
- the choice of D from simulated RTTs;
- leave and timeout schedule `RETURN_TO_AI`;
- host loss is detected;
- version, build-id and data-hash mismatches are rejected;
- keepalives flow while no round is running.

**Multi-process integration.**

- 2–3 `bitriot_headless` processes, extended with `--host`/`--join`, over native UDP on 127.0.0.1, with impairment from `VNET_IMPAIR`.
- Use **ephemeral ports**, so parallel ctest runs don't collide.
- Play an AI-heavy round with scripted inputs to its end. All processes must report identical checkpoint and final hashes and the same winner. Then play a second round started by the host.
- Run it under ctest with a timeout. It may be excluded from the fast default suite if `tests/README.md` documents that, and the coordinator still runs it.

**Desync detection.** A test-only flag perturbs one peer. `DESYNC` must fire at the first checkpoint after the perturbation, and both peers' ring-buffer dumps and records must be written.

Prove that tests fail without the code where it makes sense: for example, session tests with redundancy disabled, run under loss.

## Acceptance criteria

- **Two real windows on one machine play a full match.** For example, `--host` and `--join 127.0.0.1:<port>`. This needs a user play-test, which may be pending at merge; give the user exact commands.
- **The multi-process headless test passes under impairment.**
- **Desyncs are detected,** and their dumps and records are written.
- **Peer loss turns that player into AI. Host loss ends the match cleanly.**
- **Local play is unchanged** when no netplay flags are given.
- **`doco/netplay/protocol.md`** documents every message and the versioning.

## Outcome

*(fill in when done)*
