# 13 — Disconnects and hot-join

- **Repo:** BitRiot
- **Branch:** `netplay/13-hot-join`
- **Depends on:** 05 (record and `bitriot_headless`), 07 and 08 (overlay UI). Brief 12 is also needed for browser peers.
- **Status:** todo

## Goal

Make online matches survive people coming and going:

- A peer that drops returns its slots to AI (already in 07).
- That peer can **reconnect** and reclaim its slots.
- A **new** player can join a match in progress and take over an AI slot.

All of this is done without state snapshots. The joiner receives the match record so far and fast-forwards its own simulation.

## Read first

- Brief 07's Outcome: the leave/timeout path, `ControlEvent`s and pacing.
- Brief 05's Outcome: `MatchRecord`, `bitriot_headless` fast replay, and the sound and render mute.
- [`research/code-facts.md`](research/code-facts.md) §6 (takeover path) and §9 (sound from the sim; `MickBaseSound::setBaseInstance` can swap in a null backend).

## Design

- **The host keeps the authoritative `MatchRecord`** of the current round as it goes: `MatchConfig` plus every confirmed `TickInputs`. Memory is about 1 byte per slot per tick plus occasional control events, which is roughly 54 KB for 9,000 ticks × 6 slots.

Brief 01's per-round entity-id reset is a **hard requirement** here, because a fresh joiner process starts at id 1.
- **Hot-join flow**
  1. The new peer sends `HELLO`.
  2. The host replies `WELCOME_MIDMATCH` and streams the record so far in chunks, with flow control because a record can be larger than one datagram.
  3. The joiner fast-forwards its `Simulation` with rendering and sound off. Meanwhile it keeps buffering new `TICKS` as they arrive.
  4. When it has caught up to the live tick, it sends `READY_AT(tick)`.
  5. The host schedules `HUMAN_TAKES_OVER` for the chosen AI slot at a future tick.
  6. The joiner verifies its first live checkpoint hash. If it doesn't match, it drops out with a `DESYNC` dump rather than corrupting the match.
- **Slot choice:** the joiner picks an AI-controlled slot in the lobby-like overlay, or the host auto-assigns one. With no AI slot free, the joiner is a **spectator**: it simulates, receives `TICKS`, and sends no inputs. Spectators are cheap, so include them.
- **Reconnect:** a peer that drops and returns within the round, identified by a token issued in `WELCOME`, is offered its old slots back if they are still AI-controlled. The rest of the flow is the hot-join flow.
  - A reconnecting **browser** peer gets a new vnet context and nonce, and possibly a different vaddr, while the old relay session lingers for up to ~15 s. The game's token, not the vaddr, identifies the peer, and the host must accept the new address while the stale one is still registered.
- **Between rounds:** a joiner that arrives during the scoreboard simply joins the next round's `START`.
- **Fast-forward budget**
  - Measure how long fast-forwarding a 9,000-tick round (5 minutes) takes, native and wasm, with 4 AI players doing A*.
  - Target: under 3 s native and under 10 s wasm.
  - If wasm is too slow, record the numbers and propose options in the Outcome: periodic host-side checkpoints of the full sim state, or AI pathfinding optimisation. Don't implement snapshots without the user's agreement; they are the big job this design avoids.
- **Time-sliced fast-forward.** Fast-forward in chunks per `frame()`, for example 300–1000 ticks, and keep calling `vnet_poll`, sending keepalives and updating progress between chunks.
  - A long synchronous fast-forward would block networking. In the browser the game's own peer timeout (the host's ~10 s) is what decides a peer has dropped. The vnet relay's session timeout (~15 s, `-session-timeout`, configurable) only reclaims resources for a silent member; it does not govern drop detection.
  - Keep time-slicing for native UDP peers too: it is the same peer timeout there.
- **Pacing during the join:** the live match must not stall while someone joins. Only the joiner is behind, and the host doesn't wait for a joiner until `READY_AT`.
- **Host loss** remains out of scope (no migration), as in brief 07.

## In scope

- Host record retention.
- Chunked record transfer.
- Joiner fast-forward and catch-up.
- `READY_AT` and the scheduled takeover.
- Reconnect tokens.
- Spectators.
- UI: "Joining… catching up N%" on the joiner, and "<name> joined" on others.

## Out of scope

- State snapshots.
- Joining across a protocol or data version.
- Persistent accounts.

## Tests

- **`LockstepSession` unit tests:** a joiner mid-match receives the full record under loss, the takeover is scheduled at the agreed tick, and a spectator receives ticks and never sends inputs.
- **Multi-process:** two `bitriot_headless` peers play; a third joins at tick about 1,500, takes over an AI slot, and the match finishes with identical hashes on all three.
- **Reconnect:** kill a peer at tick X and restart it with its token. It reclaims its slot, and the hashes still agree.
- **Fast-forward benchmark,** as a test that reports numbers rather than enforcing a strict threshold in CI. Put the numbers in the Outcome.
- **Desync safety:** a joiner with deliberately corrupted state drops out at its first checkpoint, and the live match is unaffected.

## Acceptance criteria

- A player can join and leave an online match in progress, and reconnect, without anyone else noticing beyond an overlay message.
- The multi-process tests pass.
- Fast-forward numbers are recorded.

## Outcome

*(fill in when done)*
