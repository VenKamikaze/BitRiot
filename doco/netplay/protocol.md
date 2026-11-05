# Bit Riot lockstep protocol (version 2)

What goes over the wire in native netplay (brief 07; version 2 is brief 08), and the rules each side follows. The code is `src/net/`: [`NetProtocol.h`](../../src/net/NetProtocol.h) is every message (encoder, decoder, sizes), [`LockstepSession.h`](../../src/net/LockstepSession.h) is the behaviour, [`NetMatch.h`](../../src/net/NetMatch.h) joins a session to a `Simulation`. This page is what a second implementation (the browser build, brief 12; a tool in another language) needs; where it and the code disagree, the tests are the arbiter (`tests/NetProtocolTest.cpp`, `tests/LockstepSessionTest.cpp`, `tests/NetplayProcessTest.cpp`).

The settled decisions behind it are in [`README.md`](README.md): D1 (input-only lockstep), D2 (star topology, the host decides), D7 (every packet repeats what is unacknowledged), D8 (input delay, no rollback).

## Transport

[vnet](https://github.com/VenKamikaze/vnet) datagrams (`vnet_sendto`/`vnet_recvfrom`): unreliable, unordered, at most `VNET_MAX_DATAGRAM` = 1100 bytes, no ordering and no delivery guarantee on any backend. The protocol assumes nothing more, so it runs unchanged over UDP, the relay, and a reliable WebSocket (where its repeats are just redundant). A `VNET_EAGAIN`/`VNET_EIO` from a send is a lost packet and nothing else; it never ends a session.

All integers are **little-endian**, fixed width. `u8 u16 u32 u64` below. A *string* is a `u8` length, then that many bytes (at most 24 for a name, 120 for a reject text; bytes below 0x20 or 0x7F are replaced by `?` on reading so a hostile peer cannot put escape sequences in a log).

## Header

Every datagram starts with the same 8 bytes, in every version:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `'B' 'N'` |
| 2 | 1 | message type |
| 3 | 1 | sender's peer id: the host is `0`, peers are `1..5`, `0xFF` = not assigned yet |
| 4 | 4 | session id, chosen at random by the host; `0` in a `HELLO` |

A datagram is dropped (and counted in `Stats::badPackets`; it does **not** count as a sign of life, so a peer that sends only garbage times out) if the magic is wrong, the body does not decode **exactly** (truncated, trailing bytes, a count that does not fit, a value out of range), the session id is not the session's, or it did not come from the address that peer id was given to. A host only believes a packet whose source address *and* claimed peer id match a peer it knows; a peer only believes the host's address.

## Versioning

* `PROTOCOL_VERSION` is **2**. (Version 2, brief 08, added the `READY` message, the ready byte of each `LOBBY` peer and `LEAVE` reason 4. A version-1 `HELLO` is answered with `REJECT_VERSION` like any other version, and the text names both numbers.) It is a `u16`, the **first field of `HELLO`'s body**, so that a host can read the version of any `HELLO` whatever the rest of it looks like.
* A host that receives a `HELLO` of another version answers `REJECT` (`REJECT_VERSION`, nonce 0) **before it reads the rest of the body**. So a later version may change every message except the 8-byte header and the first two bytes of `HELLO`.
* Bump the version whenever the layout or the meaning of anything after the header changes.
* Separately from the protocol, **the build id and the data hash must match** (below). A mismatch is refused even when the protocol version is the same, because the same protocol between two different simulations desyncs within seconds.
* The state-hash version (`Simulation::STATE_HASH_VERSION`, 2) is not on the wire: it is part of the build, which the build id covers.

## Identity: build id and data hash

`HELLO` carries `Simulation::buildId()` (a hash of the simulation's source text; identical on GCC, Clang, aarch64 and wasm for the same source, proven by brief 05b's gate) and `Simulation::dataHash()` (the parsed `data/entity.txt` and `data/player.txt` plus the named constants in code, brief 05). The host compares both with its own and refuses a peer that differs, with a text naming the host's value and the peer's (`bitriot_headless version --items` lists what the data hash is made of).

## Messages

`T` is the type byte. `str` is a string as above.

### 1 HELLO (peer to host; repeated every 250 ms until WELCOME or REJECT)

`u16 protocol, u64 buildId, u64 dataHash, u8 localPlayers (1..6), u32 nonce, u32 token, str name`

`nonce` is picked by the peer: the answer echoes it, and a repeated `HELLO` (a lossy link, 250 ms apart) is recognised as the same peer, not a second one. `token` is a reconnect token from an earlier WELCOME (0 = none; unused in version 1, for brief 13). Header: sender `0xFF`, session `0`.

### 2 WELCOME (host to peer)

`u32 nonce, u8 peerId (1..5), u32 token, u8 slotMask, u8 maxSlots`

`peerId` is the peer's id from now on (its header's sender byte); `token` is the secret a reconnect will present (brief 13); `slotMask` is the player slots it has *for now* (the LOBBY and START messages are authoritative; slots are re-dealt as players come and go in the lobby). The header carries the session id the peer must use from here on. A `HELLO` repeated after the host already answered gets the same WELCOME again.

### 3 REJECT (host to a would-be peer, in answer to any HELLO it will not accept)

`u32 nonce, u8 reason, u16 hostProtocol, u64 hostBuildId, u64 hostDataHash, str text`

| Reason | Value | When |
|---|---|---|
| `REJECT_VERSION` | 1 | another protocol version |
| `REJECT_BUILD` | 2 | another build id |
| `REJECT_DATA` | 3 | another data hash |
| `REJECT_FULL` | 4 | not enough free player slots (6 humans at most, host included) |
| `REJECT_IN_PROGRESS` | 5 | a round is running (joining mid-round is brief 13) |
| `REJECT_BAD_REQUEST` | 6 | nonsense, e.g. 0 or 7 local players |

The checks run in this order: version, build id, data hash, then (for a `HELLO` from an address already known: the same answer again if the nonce matches) a nonsense request, a round in progress, a full session. A `REJECT` carries the host's numbers so the peer can show what differs; `text` is a sentence for the player. A peer that receives one ends with `END_REJECTED`.

### 4 LOBBY (host to every peer, every 500 ms in the lobby and between rounds, every second while a round runs, and at once when it changes)

`u8 phase (0 lobby, 1 running, 2 between rounds), u16 round (the next round's number, or the running one), u8 mapWidth, u8 mapHeight, u8 blockPercentage, u8 npcSpawner, u8 nSlots (0..6), nSlots x (u8 owner, u8 team), u8 nPeers (0..6), nPeers x (u8 id, u8 slotMask, u16 rttMs (0xFFFF = not measured), u8 ready (0 or 1), str name)`

`owner` is the peer id that plays the slot, `0xFF` = a computer player. The host (id 0) is in the peer list, and is always `ready`; a peer's `ready` is the answer the host last applied from its `READY`. Everything here is for display and for the peers' own bookkeeping: it is also the host's liveness signal in the lobby. `phase` 0 after `phase` 2 with the same `round` as the scoreboard's next one means the host went **back to the lobby** (below): a peer in its own `BETWEEN_ROUNDS` state follows it into the lobby.

### 5 START (host to every peer; repeated every 80 ms until that peer's first INPUT of the round arrives)

`u16 round, u64 seed, u8 mapWidth, u8 mapHeight, u8 blockPercentage, u8 npcSpawner, u8 nSlots (0..6), nSlots x (u8 owner, u8 team, u8 ai), u8 delay`

Everything a peer needs to build the same round: the `MatchConfig` (seed, map, blocks, spawner, and per slot its team and whether it starts as a bot) plus who plays which slot and `D`. A peer checks it as strictly as the host did (`MatchConfig::problem()`, and `1 <= delay <= 16`) and ends the session (`END_BAD_HOST`) if it is not playable. Round numbers start at 1 and rise by one; a `START` of a round the peer has already begun just makes it send an `INPUT` at once, and one that arrives while it is still finishing the previous round waits until that round is over here.

### 6 INPUT (peer to host)

`u16 round, u32 ack, u32 first, u8 count, count x record`, a record being `u8 mask, one u8 PlayerInput byte for each bit set in mask (slot order), u8 nEvents, nEvents x (u8 slot, u8 kind)`.

Record `k` is this peer's input for tick `first + k`. `mask` is the slots whose bytes were really sampled on this machine for that tick (its players; a slot taken over mid-round joins the mask from the tick the request was made). `kind` is a `ControlEvent::Kind` (0 `HUMAN_TAKES_OVER`, 1 `RETURN_TO_AI`, 2 `LEAVE`); these are *requests*, which the host may drop. `ack` is the highest tick `t` such that this peer holds the host's final inputs for every tick `1..t`. **Every INPUT repeats all the records the host has not acknowledged** (`first = host's ack + 1`), oldest first, as many as fit. An INPUT with no records is the peer's keepalive while a round runs, and the first one is the acknowledgement of START.

### 7 TICKS (host to peer)

`u16 round, u32 ack, u32 first, u8 count, u8 waitingMask, 6 x u8 owner, count x tick`, a tick being `6 x u8 PlayerInput (slots 0..5), u8 nEvents, nEvents x (u8 slot, u8 kind)`.

Tick `k` is the **final** `TickInputs` of tick `first + k`: what every machine, the host included, passes to `Simulation::step()` for that tick. `ack` is the highest tick `t` such that the host holds this peer's records for every tick `1..t`. **Every TICKS repeats all the confirmed ticks the peer has not acknowledged** (`first = peer's ack + 1`), oldest first, as many as fit (about 150 at 7 bytes a tick: five seconds of play in one datagram; the fixed part of a TICKS datagram is 26 bytes). `waitingMask` has bit `n` set for every peer the host is waiting on for its next tick, so a stalled peer can say "waiting for <name>". `owner[s]` says who plays slot `s` after the last tick actually in the packet (the round's own owners, even once the host has dealt the next round's) (a peer id, `0xFF` = a bot): a machine that asked to take a bot over learns from it whether the claim was granted (two machines can claim the same bot in the same tick, and the final inputs carry the same event either way). A TICKS with no ticks is the host's keepalive while a round runs.

A peer refuses a TICKS whose events the simulation would refuse (a slot outside the round, an unknown kind), and zeroes slot bytes beyond the round's player count.

### 8 CHECKSUM (peer to host)

`u16 round, u8 n (0..8), n x (u32 tick, u64 hash)`

The peer's `Simulation::stateHash()` after stepping each checkpoint tick (every 30th tick, and the round's last), the newest eight, sent when a new one exists and repeated every 400 ms, **also between rounds** (the round's last tick is reported as the round ends, and the host will not start the next round until it holds every peer's hash for it).

### 9 DESYNC (host to every peer; repeated every 100 ms)

`u16 round, u32 tick, u64 hostHash, u64 peerHash, u8 offender`

The host compares each peer's hash with its own for the same tick; on a difference it sends this and stops confirming ticks. `tick` is the first checkpoint at which they differ.

### 10 PING, 11 PONG

`u32 seq, u32 sentMs`

The host pings every peer (every 250 ms outside a round, every second during one); the peer answers with a PONG that repeats the body unchanged; the host measures the round trip with its own clock (`vnet_time()`, low 32 bits), keeping the last 16 samples per peer.

### 12 KEEPALIVE

`u16 round, u32 ack`

Sent when nothing else went to that machine for 250 ms in states where no round is running (the lobby, the scoreboard). While a round runs the keepalive is an empty INPUT / TICKS.

### 13 LEAVE

`u8 reason`: 0 the player quit, 1 the host is closing the session, 2 stopped because of a desync, 3 (host to a peer) you were silent too long and were dropped, 4 (host to a peer, version 2) the host removed you from the lobby. A reason above 4 does not decode.

Sent three times (the game and `bitriot_headless` then keep polling for up to 250 ms so that, under `VNET_IMPAIR` latency, they are on the wire before the context is destroyed; otherwise the others would fall back to the 10 s timeout). From a peer: the host turns its slots into bots. From the host: every peer ends its session. A peer that has been dropped but keeps talking gets a `LEAVE` (kicked) back, at most once a second.

### 14 READY (peer to host, version 2)

`u16 round, u32 seq, u8 ready (0 or 1)`

"I am / am not ready to start." Sent in the lobby only. `round` is the round the next `START` will be (the `round` of the last `LOBBY` the peer saw); the host applies it only in its own lobby and only for that round, so an answer that was in flight when a round started cannot be applied to the lobby after it. `seq` is a counter of the peer's own, starting at 1 for its first answer and never reset in a session; the host applies an answer only if its `seq` is higher than the last one it applied, so reordering and duplicates cannot undo a newer answer. It is a **state**, not an event: the peer repeats it every ~120 ms until the `LOBBY` it receives shows the host's view equal to what it wants. A peer is not ready when it joins, when a round starts, and when it comes back to the lobby; the answer is never sticky across those.

## The lobby, ready and going back to it (version 2)

* The host's `canStartRound()` in the lobby is false while a peer has not said it is ready, **if the host asked for it** (`SessionOptions::requireReady`; the game does, `bitriot_headless host` does not, so its tests and the multi-process test are unchanged). A host alone in its lobby may always start. Ready flags do not reset when the host changes the settings; the lobby shows the settings live and the peers' ready state stays as they set it.
* **Between rounds nobody is asked to be ready.** The roster is the one that played; the host either starts the next round (as in version 1) or **goes back to the lobby** (`LockstepSession::returnToLobby()`), under the same conditions as the next round (every peer holds the last tick and has reported the hash of the last checkpoint). That sets the host's state to the lobby, clears every ready flag and announces `phase 0` with the next round's number. Each peer follows when it has finished its own round (state `BETWEEN_ROUNDS`) and sees that `LOBBY`; a peer still stepping its last ticks follows a moment later. In the lobby settings can change, machines can join or leave, and everybody says ready again.
* **Removing a peer.** In the lobby (not in a round) the host can remove a peer: it sends `LEAVE` reason 4 (three times, like any leave), forgets the peer and deals the slots again. The peer ends with `END_REMOVED`. There is no ban: the same machine may send a new `HELLO`. If all three `LEAVE`s are lost, the host remembers the removed address for 30 s and answers any packet from it (at most one a second) with `LEAVE` reason 4, so the peer still ends with the right reason and not with a host timeout.
* **A joining `bitriot_headless` says ready by itself** as soon as it is in a lobby (it has no one to press a button), so a host that asks for ready (`SessionOptions::requireReady`: the game's menus; `bitriot_headless host --require-ready 1`) can start with it.

## Tick numbers, and what a peer does with them

* Tick `t` is the `t`-th `Simulation::step()` of a round, `t >= 1`: the numbering of `MatchRecord`, whose `ticks[i]` is tick `i + 1`.
* **Ticks `1..D` are empty** on every machine from the moment of START, with no packet: nobody can have sampled them yet.
* A machine that is about to step tick `T` samples its local devices (the `InputSampler` of brief 04a) and submits the sample for tick `T + D` (`LockstepSession::submitLocal`, once per stepped tick). So the record for tick `t` exists once its machine has stepped `t - D`. While a machine is stalled it samples nothing, and its events accumulate into the next sample (a stall merges two taps into one `RELEASED` bit, as 04a's follow-up said).
* A machine steps tick `T` only if it holds the host's final inputs for it (`canStep()`); otherwise the frame loop holds the tick back and the player sees "waiting for <name>". Nobody ever guesses, and there is no rollback (D8).

## The host: merging a tick

The host **confirms** tick `t` when it holds a record for `t` from itself and from every peer that has not left. It then builds the final `TickInputs`, deterministically, by going through the machines in id order (host first); for each machine:

1. its **events first**, each checked: a slot outside the round or an unknown kind is dropped; `HUMAN_TAKES_OVER` is taken only if nobody plays the slot (it is a bot's), and then the machine owns the slot from this tick on; `RETURN_TO_AI` and `LEAVE` are taken only from the slot's owner, who then no longer owns it. A dropped event leaves no trace. Accepted events go into the tick's `control` list in that order. **At most one event per slot per machine per tick** (the first) and at most 18 from machines in all, so that with the host's own leaver events (at most one per slot) a tick never holds more than 24, the limit of the wire format and of `MatchRecord`; the excess is dropped;
2. then **the bytes** of the slots the machine owns, from its record's mask (a machine's byte for a slot it does not own is ignored: a stale claim loses);
3. last, the events the host made itself: a `RETURN_TO_AI` for every slot of a peer that left or timed out since the last confirmed tick (below).

Because the events of a record apply at the tick the record belongs to, a takeover and its first inputs travel together and apply on the same tick (04b). The rule "first claim wins, in id order" makes two peers taking over the same bot in the same tick deterministic. The loser learns of it when the tick its request was for is stepped (`TICKS.owner` does not name it, or the final tick has no such event) and its game takes the slot back from its input layer (`LockstepSession::takeRejectedClaims()`, `InputHandler::revokeSlot()`).

The host does not run ahead: it can confirm `t` only after it has stepped `t - D` itself and every peer has stepped `t - D` (that is when their records exist), so nobody is ever more than `D` ticks ahead of the slowest. There is no separate pacing rule.

## Loss, duplication, reordering (D7)

* Both directions repeat everything unacknowledged in every packet and carry the ack of the other direction (INPUT's `ack` for TICKS, TICKS's `ack` for INPUT). A packet that gets through repairs every loss before it. Records and ticks already held are ignored, so duplicates and reordering are harmless.
* A sender sends when it has something new, when its ack advanced (peer), and again every 40 ms while anything is unacknowledged.
* The sender trims what is acknowledged; the host keeps confirmed ticks until every active peer has acknowledged them and it has stepped them itself.
* The wire cost is a few kilobits per peer: about 7 bytes a tick plus the repeats of the last `RTT / 33 ms` ticks.

## The input delay D (D8)

The host chooses `D` once per round, from the pings of the lobby:

```
per peer: worst = the largest of its last 16 RTT samples, jitter = their mean absolute deviation
D = clamp( ceil( max over peers (worst + jitter) / tickMs ) + 1 , 2 , 8 )        (tickMs = 33)
```

A peer's input for tick `t` is sampled when it steps `t - D` and needs a full round trip to come back confirmed, plus up to one frame of sampling and sending, hence the `+ 1`. `D = 3` is used only when nothing has been measured (a host alone). The host logs how it got its number (`LockstepSession::delayNote()`), sends it in START, and the round uses it throughout. Over a relay (brief 12) the round trip is longer, and so is D. `SessionOptions::fixedDelay` (the headless `--delay`) overrides it for tests.

A machine that holds more than `D + 2` confirmed ticks it has not stepped is asked to step up to two extra ticks per frame (`extraTicksWanted()`, wired to the frame loop's `extraTicks`). By construction the unstepped backlog is at most `D`, so this fires only if a future change (a varying D, hot-join) lets it grow; it is a safety net and its rule is tested as a function.

## Liveness and leaving

* `pump()` runs every frame **in every state**: the lobby, a round, the scoreboard. Anything heard from a machine counts as life.
* A machine silent for **10 s** is gone. The host drops a silent peer: in the lobby it just leaves the list; during a round its slots get `RETURN_TO_AI` on the next tick the host confirms (the same tick on every machine), and nobody waits for it again. A peer that sends `LEAVE` is treated the same, at once. The bot that takes over starts with its AI goals cleared (04b).
* A peer whose host is silent for 10 s ends its session (`END_HOST_LOST`) with a message and returns to the menu. There is **no host migration**. A host that leaves sends `LEAVE` (reason 1) and its peers end at once (`END_HOST_CLOSED`).
* A joining machine that gets no answer in 10 s gives up (`END_CONNECT_TIMEOUT`).
* A peer that was dropped and comes back talking is told so (`LEAVE` kicked, `END_KICKED`). Reconnecting is brief 13.

## Rounds

* The round ends on the same tick on every machine, because the end is part of the simulation (briefs 01 and 03a). Each machine tells its session (`roundFinished()`), which then only acknowledges and keeps the connection alive until the next START. Every machine shows the scoreboard.
* **Only the host starts the next round**, with a new seed. The scoreboard's Play Again does nothing on a peer, and is greyed out (the game labels it "waiting for the host").
* The host may start the next round only when every peer has acknowledged the last tick of this one (`canStartRound()`); otherwise a slow peer would be left waiting for ticks the new round no longer sends. Every packet of a round carries its round number, and a packet of another round is ignored, so a late repeat of round 1 cannot reach round 2.
* A machine joining between rounds (`REJECT_IN_PROGRESS` only applies while a round runs) is dealt slots in the next START.

## Desync handling

Peers report checkpoints, the host compares (above). By the time DESYNC arrives the machines are past the tick in question, and undefined behaviour might not replay the same way, so **each machine keeps a ring of state dumps** (`Simulation::dumpState()`) for its last 16 checkpoint ticks (`NetMatch`). On DESYNC every machine writes, to the output directory (the working directory by default):

* `desync-r<round>-t<tick>-p<peer id>-state.txt`: its state dump at the first disagreeing checkpoint (if it is still in the ring, which at 16 checkpoints is about 16 seconds);
* `desync-r<round>-t<tick>-p<peer id>-now-t<current tick>-state.txt`: its state where it stopped;
* `desync-r<round>-t<tick>-p<peer id>.brmr`: its `MatchRecord` of the round so far (the final inputs and a checkpoint every 30 ticks: `bitriot_headless replay` accepts it).

and the match stops (the host lingers 1.5 s so the DESYNC reaches everyone, then leaves) with a message naming the files. `diff` of two machines' `-state.txt` finds the first diverging field.

## What versions 1 and 2 do not do

No reconnect (the token is issued and unused), no moving a peer to another slot (slots are dealt in machine order: the host's, then each peer's, then computer players), no join mid-round, no host migration, no spectators, no encryption or authentication (a peer is whoever sends the right session id from the right address; the session id is random but travels in the clear), and no NAT traversal (vnet has none: one side needs a reachable UDP port, or a relay).
