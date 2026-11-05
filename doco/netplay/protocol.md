# Bit Riot lockstep protocol (version 3)

What goes over the wire in native netplay (brief 07; version 2 is brief 08, version 3 is brief 13), and the rules each side follows. The code is `src/net/`: [`NetProtocol.h`](../../src/net/NetProtocol.h) is every message (encoder, decoder, sizes), [`LockstepSession.h`](../../src/net/LockstepSession.h) is the behaviour, [`NetMatch.h`](../../src/net/NetMatch.h) joins a session to a `Simulation`. This page is what a second implementation (the browser build, brief 12; a tool in another language) needs; where it and the code disagree, the tests are the arbiter (`tests/NetProtocolTest.cpp`, `tests/LockstepSessionTest.cpp`, `tests/NetplayProcessTest.cpp`).

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

* `PROTOCOL_VERSION` is **3**. (Version 2, brief 08, added the `READY` message, the ready byte of each `LOBBY` peer and `LEAVE` reason 4. Version 3, brief 13, added a flags byte to `HELLO` and the messages `WELCOME_MIDMATCH`, `READY_AT` and `TAKEOVER_AT`, for joining a round in progress, reconnecting and watching. A version-1 or version-2 `HELLO` is answered with `REJECT_VERSION` like any other version, and the text names both numbers.) It is a `u16`, the **first field of `HELLO`'s body**, so that a host can read the version of any `HELLO` whatever the rest of it looks like.
* A host that receives a `HELLO` of another version answers `REJECT` (`REJECT_VERSION`, nonce 0) **before it reads the rest of the body**. So a later version may change every message except the 8-byte header and the first two bytes of `HELLO`.
* Bump the version whenever the layout or the meaning of anything after the header changes.
* Separately from the protocol, **the build id and the data hash must match** (below). A mismatch is refused even when the protocol version is the same, because the same protocol between two different simulations desyncs within seconds.
* The state-hash version (`Simulation::STATE_HASH_VERSION`, 2) is not on the wire: it is part of the build, which the build id covers.

## Identity: build id and data hash

`HELLO` carries `Simulation::buildId()` (a hash of the simulation's source text; identical on GCC, Clang, aarch64 and wasm for the same source, proven by brief 05b's gate) and `Simulation::dataHash()` (the parsed `data/entity.txt` and `data/player.txt` plus the named constants in code, brief 05). The host compares both with its own and refuses a peer that differs, with a text naming the host's value and the peer's (`bitriot_headless version --items` lists what the data hash is made of).

## Messages

`T` is the type byte. `str` is a string as above.

### 1 HELLO (peer to host; repeated every 250 ms until WELCOME or REJECT)

`u16 protocol, u64 buildId, u64 dataHash, u8 localPlayers (1..6), u32 nonce, u32 token, u8 flags, str name`

`nonce` is picked by the peer: the answer echoes it, and a repeated `HELLO` (a lossy link, 250 ms apart) is recognised as the same peer, not a second one. `token` is a reconnect token from an earlier WELCOME (0 = none): it names the *player*, whatever address the packet comes from (below). `flags` (version 3): bit 0 `HELLO_SPECTATE`, "do not give me a player slot"; any other bit makes the `HELLO` undecodable. Header: sender `0xFF`, session `0`.

### 2 WELCOME (host to peer)

`u32 nonce, u8 peerId (1..5), u32 token, u8 slotMask, u8 maxSlots`

`peerId` is the peer's id from now on (its header's sender byte); `token` is what a reconnect will present (version 3: see "Reconnecting"); `slotMask` is the player slots it has *for now* (the LOBBY and START messages are authoritative; slots are re-dealt as players come and go in the lobby). The header carries the session id the peer must use from here on. A `HELLO` repeated after the host already answered gets the same WELCOME again.

### 3 REJECT (host to a would-be peer, in answer to any HELLO it will not accept)

`u32 nonce, u8 reason, u16 hostProtocol, u64 hostBuildId, u64 hostDataHash, str text`

| Reason | Value | When |
|---|---|---|
| `REJECT_VERSION` | 1 | another protocol version |
| `REJECT_BUILD` | 2 | another build id |
| `REJECT_DATA` | 3 | another data hash |
| `REJECT_FULL` | 4 | not enough free player slots (6 humans at most, host included) |
| `REJECT_IN_PROGRESS` | 5 | a round is running and this host does not take late joiners (`SessionOptions::allowHotJoin` is off), or the round has stopped (a desync) or is longer than the host keeps a record of (1,048,576 ticks). Since version 3 a host normally lets a joiner in mid-round (`WELCOME_MIDMATCH`). |
| `REJECT_BAD_REQUEST` | 6 | nonsense, e.g. 0 or 7 local players |

The checks run in this order: version, build id, data hash, then (for a `HELLO` from a machine already known, by token or by address: the same answer again if the nonce and address match; for a removed machine's address and nonce: `LEAVE` reason 4) a nonsense request, a round in progress (version 3: only when late joiners are off or the round cannot be joined), a full session (no free peer id mid-round). A `REJECT` carries the host's numbers so the peer can show what differs; `text` is a sentence for the player. A peer that receives one ends with `END_REJECTED`.

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

### 15 WELCOME_MIDMATCH (host to peer, version 3)

`u32 nonce, u8 peerId (1..5), u32 token, u16 round, u32 head, u8 offerMask`

The answer to a `HELLO` while a round is running, instead of `WELCOME`: the same identity (`nonce`, `peerId`, `token`; the header carries the session id), plus the round in progress (`round`, at least 1), the newest tick the host has confirmed (`head`) and `offerMask`: the slots this peer is *offered back* because they were its own before it dropped (a reconnect with its token) and are still bots; 0 for a new joiner. The host repeats it every 250 ms with a fresh `head` while the joiner catches up (it is how the joiner knows how far it has to go and shows "catching up N%"), and answers a repeated `HELLO` with it. The joiner treats it like `WELCOME` (its state is the lobby's until the `START` of that round arrives).

### 16 READY_AT (late joiner to host, version 3)

`u16 round (1 or more), u32 tick`

"I have stepped through `tick` and hold everything the host has confirmed: schedule me." Sent every 120 ms by a late joiner that has caught up, until `TAKEOVER_AT` arrives. The host answers each one with `TAKEOVER_AT` once it has decided, and ignores one from a machine that did not join mid-round, for another round, or while the round is not running.

### 17 TAKEOVER_AT (host to late joiner, version 3)

`u16 round, u32 tick, u8 slotMask, u8 reason`

From `tick` on this machine plays `slotMask` (bit = slot) and the host waits for its records from `tick`; the `HUMAN_TAKES_OVER` events are in the final inputs of tick `tick`, for everybody (the host puts them there itself, like a leaver's `RETURN_TO_AI`). `slotMask` 0 means the machine only watches, and `reason` says why: 0 `TAKEOVER_GRANTED` (iff the mask is not 0), 1 `TAKEOVER_NO_FREE_BOT`, 2 `TAKEOVER_SPECTATOR` (it asked: `HELLO_SPECTATE`), 3 `TAKEOVER_TOO_SLOW` (see below). A mask with a reason, or no mask without one, does not decode.

## The lobby, ready and going back to it (version 2)

* The host's `canStartRound()` in the lobby is false while a peer has not said it is ready, **if the host asked for it** (`SessionOptions::requireReady`; the game does, `bitriot_headless host` does not, so its tests and the multi-process test are unchanged). A host alone in its lobby may always start. Ready flags do not reset when the host changes the settings; the lobby shows the settings live and the peers' ready state stays as they set it.
* **Between rounds nobody is asked to be ready.** The roster is the one that played; the host either starts the next round (as in version 1) or **goes back to the lobby** (`LockstepSession::returnToLobby()`), under the same conditions as the next round (every peer holds the last tick and has reported the hash of the last checkpoint). That sets the host's state to the lobby, clears every ready flag and announces `phase 0` with the next round's number. Each peer follows when it has finished its own round (state `BETWEEN_ROUNDS`) and sees that `LOBBY`; a peer still stepping its last ticks follows a moment later. In the lobby settings can change, machines can join or leave, and everybody says ready again.
* **Removing a peer.** In the lobby (not in a round) the host can remove a peer: it sends `LEAVE` reason 4 (three times, like any leave), forgets the peer and deals the slots again. The peer ends with `END_REMOVED`. There is no ban: the same machine may send a new `HELLO`. If all three `LEAVE`s are lost, the host remembers the removed address for 30 s and answers any packet from it (at most one a second) with `LEAVE` reason 4, so the peer still ends with the right reason and not with a host timeout.
* **A joining `bitriot_headless` says ready by itself** as soon as it is in a lobby (it has no one to press a button), so a host that asks for ready (`SessionOptions::requireReady`: the game's menus; `bitriot_headless host --require-ready 1`) can start with it.

## Joining a round in progress, reconnecting and watching (version 3)

Nothing here needs a state snapshot (D1): a round is its `MatchConfig` and the final `TickInputs` of every tick, and the host keeps both.

**The host keeps the record.** From a round's `START` the host keeps every confirmed tick (`TickInputs`, about 7 bytes a tick on the wire, 32 in memory: some 54 KB per 9,000 ticks and six slots on the wire; `SessionOptions::historyLimit`, 1,048,576 ticks, bounds it). The records `NetMatch` keeps (`MatchRecorder`) are the same ticks, a few steps behind; the session keeps its own because it is the authority on what was confirmed.

**The flow.**

1. A `HELLO` while a round runs is answered with `WELCOME_MIDMATCH` (the joiner is a *late joiner*, peer id as usual) and the host repeats the round's `START` until the joiner's first `INPUT` of the round arrives, as for any peer. The `START` carries the round's *initial* `MatchConfig` and slot owners, so the joiner builds the same starting simulation as everybody did, with no slot of its own.
2. **The record is streamed as `TICKS`**, from tick `D + 1` (ticks `1..D` are empty everywhere), the same message and the same ack (the joiner's `INPUT`s acknowledge the contiguous ticks it holds). A window of up to 7 or 8 datagrams (the first tick of the last one at most 1,000 ticks past the acknowledgement, so about 1,000 to 1,150 ticks in flight) is sent whenever the joiner's acknowledgement moves, so the speed is a window per round trip; if it has not moved for 100 ms the first two are sent again. That is the flow control, and it is loss-tolerant like the rest (D7). Close to the head (within 150 ticks) the ordinary `TICKS` repeat takes over and the joiner is simply a peer whose ack is a little behind. A tick that arrived out of order is kept (so one loss costs one round trip, not a window).
3. **The joiner fast-forwards**: it steps the record with no devices, no clock and no drawing, and the game mutes the sound for it (`SoundMute`, a silent `MickBaseSound` installed around the slice: the simulation plays through `MickBaseSound::getInstance()`, so no simulation source changes), in slices (the game: 8 ms per frame; `NetMatch::fastForward`), calling `pump()` between them so the network, its keepalives and its acks keep running. It records the round as it steps (so its record is byte-identical to everybody's), takes the checkpoint hashes (every 30th tick) and reports them in `CHECKSUM` as usual, but keeps no state dump for them on the way.
4. **The live match does not wait.** The host does not wait for the joiner's records (not for its next tick and not for its `READY_AT`): only for a machine it has given a slot, and only from that slot's takeover tick. `waited(t)` is false for a spectator and for a late joiner before its takeover tick.
5. **`READY_AT`**: the joiner has stepped everything it holds, and what it holds is the newest tick the host said it has (`WELCOME_MIDMATCH.head`). The host decides when it holds three things:
   * the joiner is near the live tick (its `tick` within `D + 8` ticks plus its round trip of the host's newest confirmed tick; if not, it says it again);
   * the joiner's state **agrees with the host's**: one of its `CHECKSUM`s equalled the host's hash for the same tick. The joiner's fast-forward reports every checkpoint it passed, so the host normally has one to compare at once; a join in the first 30 ticks of a round is verified at the first live checkpoint. A late joiner whose checksum **differs** is not a desync of the match: the host sends `DESYNC` to *that machine only* (three times), drops it (its late packets are answered with `LEAVE` reason 2) and carries on; the joiner writes its evidence like any desync and leaves. A late joiner that never reports is dropped after the 10 s timeout. Once one of its hashes has matched, it is trusted like anyone (but a mismatch of a machine that joined mid-round, or that only watches, always ends just that machine, never the match: a spectator's state decides nothing);
   * the joiner's **round trip fits this round's `D`**: `chooseDelay(worst of its last three round trips) <= D`. A machine that needs a longer delay than the round has would make the host wait for it on every tick, so it watches (`TAKEOVER_TOO_SLOW`) instead of stalling everyone. (The host pings a joiner that is being decided every 250 ms, and waits for two samples or two seconds.)
6. **The slots.** A joiner is dealt, from the bots that are free, no more than the players it said it has (`HELLO.localPlayers`): first the ones it was offered back (below), then the lowest numbers. A bot is free if nobody plays it, no other late joiner has been promised it, and the host's game says it is worth taking (`LockstepSession::setSlotFilter`: the game asks the simulation whether the player is alive: a takeover of a dead player does nothing). No free bot: the joiner is a *spectator* (`TAKEOVER_NO_FREE_BOT`). A `HELLO_SPECTATE` machine is never dealt one.
7. **`TAKEOVER_AT`**: the host picks the takeover tick `Ts = head + D + 2 + ceil(RTT / tickMs)`, reserves the slots (nobody else's `HUMAN_TAKES_OVER` for them is granted before `Ts`: the first claim in id order rule would otherwise let a lower peer id win), and answers `READY_AT` with `TAKEOVER_AT`. From now on it waits for the joiner's records from `Ts`. At `Ts` the merge puts a `HUMAN_TAKES_OVER` for each dealt slot in the final tick (never refused or delayed: they are counted out of the peers' share, 18 minus the takeovers of that tick, so a merged tick still holds at most 24 events with a leaver's `RETURN_TO_AI`s), exactly as if a peer had claimed it, and `owners` in `TICKS` follows. The joiner makes records from `Ts` on; those for ticks it has already stepped past (the message was slow) are neutral inputs, so the host never waits for a record nobody can make. `TAKEOVER_AT` is repeated by the host for every `READY_AT` the joiner sends before it has it.
8. **Spectators** (a late joiner with no slot, or a `HELLO_SPECTATE` machine in the lobby or later): they simulate and receive `TICKS`, send an empty `INPUT` (acks) as their keepalive and no records, and are never waited for, so a spectator cannot slow the match. The host does not count them for slots or `D`, does not ask them to be ready, and a spectator does not hold up the next round (a `START` of a newer round simply replaces the round a spectator or a late joiner was still catching up on).

**A round that ends while a joiner is catching up:** the joiner finishes the record (the end of the round is in it) and shows the scoreboard; the host does not wait for it to start the next round, nor (R3) drop it for not having reported the last checksum. If the host goes **back to the lobby** meanwhile, a machine still catching up (or watching) follows the `LOBBY` of the next round's number and leaves the old round, like a peer on the scoreboard; without that it would stay in the round for good and keep the host's ready gate shut.

**The six-human cap and late joiners.** A late joiner is not held to the cap (it asks for a bot, not a seat). Who watches is decided **afresh with every roster** (a `HELLO_SPECTATE` machine always does, for the session). If when the roster is dealt (the next round's `START`, or the lobby) the slots have run out for a late joiner, it is not dealt one and watches that round: not waited for, not asked to be ready. The machine learns it from `START` itself: it owns no slot in it, so it sends no records, shows "Watching", and (like a late joiner catching up, or a `HELLO_SPECTATE` machine) leaves a round that is still running when the host's `LOBBY` of the next round arrives. When a seat is free at a later roster it is dealt it again.

**Between rounds** nothing special happens: a machine that arrives while the scoreboard is up joins the next `START` as any peer (dealt a slot by the lobby rules), and a `HELLO_SPECTATE` machine is never dealt one.

### Reconnecting

A peer that drops (its process was killed, its link went away for good, the host timed it out) and comes back is **identified by its token, not by its address**: the `HELLO` carries the `token` an earlier `WELCOME` (or `WELCOME_MIDMATCH`) gave it. The host looks a token up before it looks at the address, so the new `HELLO` may come from another address (a browser's new relay context has a new vaddr and nonce) while the stale one is still registered:

* In the lobby or between rounds the old entry is replaced in place (same peer id, same token), whatever the address.
* In a running round the old incarnation is dropped on the spot, *without waiting for the 10 s timeout*: its slots become bots on the next confirmed tick (`RETURN_TO_AI`, the same tick on every machine), and the new one is admitted as a late joiner with the same peer id and token. **What it owned is offered back** (`WELCOME_MIDMATCH.offerMask`) and is dealt to it first at `TAKEOVER_AT`, if those slots are still bots; a slot someone took in the meantime is not (it gets a free bot instead, or watches).
* **A token only counts if its old owner has gone quiet.** A `HELLO` whose token belongs to a machine that is still talking from *another address* is held back (not answered; the peer repeats it) for 4 s: a crashed machine has been silent for 3 s by then and the token is honoured, but if the old one is still alive (two windows sharing one prefs file) the `HELLO` is a new player and the token is ignored, so two live machines never evict each other. The 3 s sit above the sparse pump of a throttled browser tab (about once a second; brief 12 tolerates gaps up to 1.5 s), so a live but throttled tab is not evicted; the 4 s must stay above the 3 s. The same address (a restart on the same socket), a gone member, or a member silent for 3 s honours the token at once. A crash-restart therefore reclaims its slot after about 3 s.
* A token the host does not know (another session, a removed player) is a new player's `HELLO`: the token is ignored. A removed machine's token is invalid (`kick()` forgets it).
* A packet from a dropped peer's old address (its process is still alive, only the link was cut) is answered with `LEAVE` reason 3 as before; the player can then come back with its token.
* A token is not a secret: it travels in the clear like the session id, and whoever sends one *is* that player. The protocol has no authentication (below).

### Removing a peer, in a round too, and a lost WELCOME

`kick(peerId)` works in every state now (it was the lobby only): in a round the peer's slots go to the AI like any leaver's, and its token dies. The host remembers a removed machine's **address and nonce** for 30 s (checked before any member lookup, so also in a round, where a removed member stays as a gone entry), so a machine still CONNECTING whose `WELCOME` was lost (and which never learned it was admitted, so it ignored the `LEAVE`) is answered with `LEAVE` reason 4 when it repeats the `HELLO` (a `HELLO` with a new nonce, a restarted game, is not a repeat and is let in: still no ban). A peer in the CONNECTING state accepts that `LEAVE` and ends with `END_REMOVED`.

### The last checksum of a round

The host does not start the next round until every peer that played has reported the hash of the round's last tick (so a desync in the last ticks cannot race the next `START`). That wait is bounded (`SessionOptions::finalHashWaitMs`, 8 s): a peer that holds every tick but never reports it is dropped (`LEAVE` reason 3) and the next round can start (07's review, R3).

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
D = clamp( ceil( max over peers (worst + jitter) / tickMs ) + 1 , 2 , maxDelay )   (tickMs = 33; maxDelay = 8, 12 on a relay)
```

A peer's input for tick `t` is sampled when it steps `t - D` and needs a full round trip to come back confirmed, plus up to one frame of sampling and sending, hence the `+ 1`. (That `+ 1` assumes the peer's inputs leave within a frame of being sampled. A browser tab driven from network events, brief 12, flushes the session right after each frame's steps for exactly that reason; a tab that went hidden after the lobby pings were taken still answers more slowly than they said, and plays at about `D / RTT` ticks a second until the next round chooses a new D.) `D = 3` is used only when nothing has been measured (a host alone). The host logs how it got its number (`LockstepSession::delayNote()`), sends it in START, and the round uses it throughout. Over a relay (brief 12) the round trip is two hops and the limit is 12 (`SessionOptions::maxDelay`; the clamp is a host-side choice, a peer plays whatever D START announces, so this is not a wire change). `SessionOptions::fixedDelay` (the headless `--delay`) overrides it for tests.

A machine that holds more than `D + 2` confirmed ticks it has not stepped is asked to step up to two extra ticks per frame (`extraTicksWanted()`, wired to the frame loop's `extraTicks`). By construction the unstepped backlog is at most `D`, so this fires only if a future change (a varying D, hot-join) lets it grow; it is a safety net and its rule is tested as a function.

## Liveness and leaving

* `pump()` runs every frame **in every state**: the lobby, a round, the scoreboard. Anything heard from a machine counts as life.
* A machine silent for **10 s** is gone. The host drops a silent peer: in the lobby it just leaves the list; during a round its slots get `RETURN_TO_AI` on the next tick the host confirms (the same tick on every machine), and nobody waits for it again. A peer that sends `LEAVE` is treated the same, at once. The bot that takes over starts with its AI goals cleared (04b).
* A peer whose host is silent for 10 s ends its session (`END_HOST_LOST`) with a message and returns to the menu. There is **no host migration**. A host that leaves sends `LEAVE` (reason 1) and its peers end at once (`END_HOST_CLOSED`).
* A joining machine that gets no answer in 10 s gives up (`END_CONNECT_TIMEOUT`). Over a relay the 10 s start when the relay's transport is ready: while the vnet context is still `CONNECTING` (a WebTransport attempt falling back to the WebSocket, a slow handshake) the clock is held at zero, and a relay that fails ends the session with the relay's own reason (`END_NETWORK`).
* **Pumps of our own that are far apart (brief 12; `SessionOptions::tolerateSparsePump`, on in the game and the headless runner).** If this machine's `pump()` calls are more than 1.5 s apart (a hidden browser tab, a sleeping laptop, a debugger), every machine's "last heard" and the connect clock are set to now before anything is read, so a gap of our own is never held against anybody; what the others sent meanwhile is still queued and is read at once. Two edge cases follow. A tab under Chrome's intensive throttling (a pump once a minute) never times a dead host out while it is hidden, and finds out within 10 s of being pumped regularly again. And a host that hiccups for more than 1.5 s at least once every 10 s keeps resetting its peers' silence clocks, so a peer that really died is dropped later than 10 s, for as long as the hiccups last. Neither affects the lockstep itself (a dead machine still stalls the tick it owes); they only delay the verdict. **With brief 13's clocks:** the same gap is also added to the host's "ready but never verified" timer, the wait for a peer's last checksum (`finalHashWaitMs`) and the pending-token grace, so a host that was not pumped for a while does not time a joiner or a peer out for it; the token rule (`TOKEN_QUIET_MS`, 3 s) is longer than the 1.5 s gap bound on purpose (an old owner whose own pumps were sparse is not mistaken for a dead one), and after a gap the quiet clock starts again from the resume. A joiner that is replaying the host's record is pumped every frame (`netFrame()`), sends its acks after each slice, and is therefore never silent for the host. The takeover rule "too slow" (`TAKEOVER_TOO_SLOW`) compares what the joiner needs (its round trip, **not** clamped to `maxDelay`) with the round's D, so a round on a relay with D = 12 still refuses a machine that needs 26.
* A peer that was dropped and comes back talking is told so (`LEAVE` kicked, `END_KICKED`); one that comes back with a new `HELLO` and its token gets its slots back (above).

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

## What versions 1 to 3 do not do

No moving a peer to another slot (slots are dealt in machine order: the host's, then each peer's, then computer players; a late joiner is dealt the lowest free bots), no turning a spectator into a player mid-round (it joins the next round as a player), no host migration, no state snapshots (a joiner replays the record: 9,000 ticks take about 0.13 s natively and about 0.25 s in wasm under Node, see brief 13), no encryption or authentication (a peer is whoever sends the right session id from the right address; the session id is random but travels in the clear), and no NAT traversal (vnet has none: one side needs a reachable UDP port, or a relay).
