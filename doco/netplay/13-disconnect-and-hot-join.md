# 13 — Disconnects and hot-join

- **Repo:** BitRiot
- **Branch:** `netplay/13-hot-join`
- **Depends on:** 05 (record and `bitriot_headless`), 07 and 08 (overlay UI). Brief 12 is also needed for browser peers.
- **Status:** done (play-test pending)

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

**Status: done, play-test pending.** A player can join a round in progress and take over a bot, reconnect after a crash or a dropped link and get the same slot back, or just watch; the others see a toast and nothing else. **No `bitriot_sim` source changed**: the build id is still `0x6767612d056ba8af` (GCC and Clang), the six golden digests and `tests/data/golden-round.brmr` are untouched, and `scripts/determinism-gate.sh --only gcc-O2,wasm-O2` passes. The game was driven under Xvfb with three real windows (evidence, not a play-test): a window joining a running round showed "Carol joined" on the others and "You are player 3" with a keyboard badge on its own, all pictures identical; a killed and restarted window got "You are player 2" back with no flag but `--join` (the token is kept in the prefs file); a window 200 ms away got "Watching - your connection is too slow for this round's input delay"; the join banner ("Joining... waiting for a free player") showed under simulated latency and loss. Not checked: a pad in any of it, a browser (12 is not merged), more than three windows.

**What landed**

1. **Protocol version 3** (`protocol.md` updated; a version-2 `HELLO` is refused with `REJECT_VERSION`): `HELLO` gets a flags byte (`HELLO_SPECTATE`), and three messages: `WELCOME_MIDMATCH` (the answer to a `HELLO` while a round runs: identity, round, the host's newest tick, and the slots offered back), `READY_AT` (the joiner has caught up) and `TAKEOVER_AT` (from this tick you play these slots, or you watch, and why). Strict decoders and `NetProtocolTest` for all of it.
2. **The host keeps the record** (`LockstepSession::m_history`, every confirmed tick, bounded by `SessionOptions::historyLimit`) and **streams it as ordinary `TICKS`**, windows of 8 datagrams per acknowledgement, no new chunk message: the redundant, acknowledged stream D7 already gave is the flow control. The joiner builds the round from the same `START` and **fast-forwards** (`NetMatch::fastForward`: no devices, no clock, no state dumps; the game does 8 ms of it per frame, the headless runner 20 ms; `pump()` runs between slices so keepalives and acks continue). Its record comes out byte-identical to everyone's.
3. **The takeover is scheduled by the host**: once the joiner says `READY_AT` near the live tick, one of its `CHECKSUM`s has matched the host's, and its round trip fits this round's D, the host deals it bots (offered-back slots first, then the lowest; only bots the game says are alive, `setSlotFilter`) at tick `Ts = head + D + 2 + RTT`, reserves them (nobody else can claim them before `Ts`), puts the `HUMAN_TAKES_OVER` in tick `Ts` of the final inputs and waits for the joiner's records from `Ts`. Before that, a late joiner is **never waited for** (`Member::waited`).
4. **Safety.** A late joiner whose hash differs is told alone (`DESYNC` to it) and dropped; the match is untouched and the joiner writes its evidence. One whose round trip needs a longer D than the round has **watches instead of stalling everybody** (`TAKEOVER_TOO_SLOW`). A joiner is only trusted once a checksum of it has matched.
5. **Reconnect.** The `HELLO` token names the player whatever the address: the host looks the token up before the address, drops the old incarnation at once (no 10 s wait; its slots go to the AI on the next confirmed tick), keeps the peer id and token, admits the new process as a late joiner and offers back what it owned if those are still bots. Browser-style "new address while the stale one is registered" is exactly this path (tested with a second loopback context). `kick()` works in a round (token dies); the removed-machine memory now holds the nonce, so a machine still CONNECTING whose `WELCOME` was lost is told (08's nit); `canStartRound`'s wait for the last checksum is bounded (07's R3: 8 s, then the peer is dropped).
6. **Spectators.** `HELLO_SPECTATE` (`--spectate`) or no free bot: they simulate and ack, send no input records, are never waited for, not dealt slots, not asked to be ready, and a newer round's `START` replaces whatever they were catching up on.
7. **Game side.** `GameEngine::netJoinFrame` (sliced fast-forward, `InputHandler::grantSlot`, "You are player N"), `canStep` is false while catching up, the overlay's join box (`NetGame::joinText`, `joinSentence`), "<name> joined" toasts (from the lobby view), the token saved in `netplay.txt` with its host and sent again on the next join to that host (the error page's Back, then Connect, is the reconnect; `--join` does it by itself), `--spectate`, `bitriot_headless join --spectate|--token|--token-file`.

**Fast-forward numbers** (the brief asks for 9,000 ticks, four bots, A*; target native under 3 s, wasm under 10 s). A 33x19 map, 75% blocks, seed 1 (four bots; the round does not end within 20,000 ticks, other seeds and maps are in the same range):

| | 9,000 ticks |
|---|---|
| native GCC Release, `FastForwardBenchmark` (step + record + a hash every 30 ticks, as `NetMatch` does) | **0.13 s** (about 70,000 ticks/s) |
| native, `bitriot_headless replay` of the same record | 0.11 s |
| wasm (`-O2`, `-fwasm-exceptions`), Node 22, `node bitriot_headless.js replay`, end to end including about 0.2 s of start-up and data loading | **0.23 to 0.34 s** (four and six players, three configurations) |

Measured with a headless wasm build from `scripts/determinism-gate.sh --only gcc-O2,wasm-O2` (`-DBITRIOT_BUILD_GAME=OFF`, as 05b's gate does) and the same record replayed natively and in Node; a browser's V8 should be in the same range. **Fast-forward is a non-issue by a factor of 30 to 40, so no state snapshots are needed and none were built.** The cost of a join is the stream (a window per round trip, 1,100 ticks: 9,000 ticks over a 100 ms round trip is about a second) and the host's one-time work, which is nothing.

**Tests** (GCC Release; Clang Release identical)

- `bitriot_sim_tests` 532 to **570**, `bitriot_tests` 678 to **718**; `ctest` 3/3; `bitriot_netplay_process_tests` 8 to **12** cases (about two minutes); `-DBITRIOT_NETPLAY=OFF`: 413 and 529; web smoke 41 of 41. What each file proves is in `tests/README.md` ("Disconnects and hot-join").
- New: `HotJoinTest` (27 cases with the reconnect and `NetMatch` ones, plus 5 more in `LockstepSessionTest`; session level: late joiner at 0, 5 and 25% loss, two slots, slow or frozen joiner, spectators, wrong-state joiner, too-slow joiner, backfill, reconnect from another address, old slot preferred, taken slot, unknown token, kick mid-round, a lost `WELCOME` and the nonce, R3, a v2 `HELLO`, `NetMatch::fastForward`), `FastForwardBenchmarkTest`, `NetProtocolTest` (+1 and the lists), 4 process cases (a third process joins at tick about 1,500 and all three print and record the same; `kill -9` at tick 600 and restart with the token; a joiner perturbed during its fast-forward stops while the match never notices; a spectator), `InputControlTest` (`grantSlot`), `LobbyModelTest` (token prefs, join sentences), `LobbyLayoutTest` (join box), `GameOptionsTest` (`--spectate`).
- **Proof they fail without the change.** The new API cannot be compiled against old code, so 24 mutations were applied one at a time to the sources, the test executable rebuilt and the relevant tests run (scratch script, not committed). 22 killed at once: the host waiting for a late joiner from the start (9 tests); a reserved bot claimable by someone else; a late joiner's mismatch stopping the whole match; a slot dealt before the joiner's state agreed; no token lookup (5); the offered slots ignored; a removed machine's nonce forgotten; no bound on the last checksum; the record sent from `m_ticks` instead of the history (19); a newer round not superseding a catch-up; a too-slow connection getting a slot; the slot filter ignored; a spectator waited for; the history limit unapplied; the `HELLO` spectate flag ignored; fast-forward keeping dumps; kick not working mid-round; the old incarnation not dropped on a reconnect; spectator records produced; unknown `HELLO` flag bits accepted; the version left at 2 (3). Two survived the first tests and each got a test (a knob `testDropTakeoverForMs` makes the late-`TAKEOVER_AT` backfill reachable, `testReadyAtLag` a joiner that is not near the live tick; both now killed). One survives as an **equivalent mutant**: never repeating the first two catch-up datagrams after 100 ms (`CATCHUP_RESEND_MS`): the 250 ms keepalive `TICKS` repairs the same loss, so it only changes speed.
- **Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, GCC Release, from the build directory, three at a time, tree not rebuilding): `bitriot_tests` (718) **0 failing seeds**, `bitriot_sim_tests` (570) **0 failing seeds**. No simulation source changed, so there is no "before" (08's 0 and 0 stand).
- Real-time tests (before the review fix round): every run of `bitriot_netplay_process_tests` passed first time (GCC ctest, Clang direct); no rerun was needed.

**Decisions** (the brief left these open; the option judged best was taken)

1. **No new "chunk" message.** The record rides the existing `TICKS` stream with an ack-clocked window, so loss, reordering and a reliable transport all work unchanged, and the joiner steps ticks as they arrive. Cost: none seen; the new messages are only for the handshake.
2. **The host's record is the session's own `m_history`, not `NetMatch`'s `MatchRecorder`** (07's follow-up suggested serialising the recorder): the session is the authority on *confirmed* ticks (the recorder is up to D behind), needs no `Simulation`, and the unit tests can run it. `NetMatch` still records on every machine (the joiner's recorder is fed by its fast-forward, so its record is byte-identical).
3. **Verification before a slot, by the existing `CHECKSUM`s**, not a hash in `READY_AT`: the joiner's fast-forward reports its checkpoints anyway, the host compares them with its own log (64 checkpoints, about 32 s), and a match marks the joiner trusted. A joiner that joins before the first checkpoint is verified at the first live one. Alternative (the brief's "first live checkpoint after takeover"): rejected, it would put an unverified machine's inputs in the match.
4. **A late joiner's desync ends only that joiner**, never the match (it owns nothing yet, or one bot: the host keeps playing; the bot goes back to the AI). Real nondeterminism in a *joiner* is therefore not fatal to the others but is logged and the joiner writes the evidence.
5. **A joiner whose connection needs a longer D than the round's watches** (`TAKEOVER_TOO_SLOW`): D is fixed per round, and a slot for a far-away machine would make everybody wait on every tick. Found in the Xvfb run: a 300 ms-latency joiner stalled the host. The check uses the last three round trips with the round's own formula; the host pings a joiner being decided every 250 ms.
6. **Slot choice is the host's** (offered slots, then the lowest live bots, as many as the joiner has players): the overlay slot picker in the brief is not built; a joiner that wants another bot can use the ordinary join selector once it is a participant. A spectator cannot claim one later (it sends no inputs); it is dealt a slot at the next round's `START`.
7. **Reconnect is the hot-join flow with a token**, and the old incarnation is dropped at once rather than after the 10 s timeout, so a crashed player is not waited for. Tokens are not secrets (no authentication in this protocol; documented).
8. **`kick()` works in a round and kills the token.** No ban (08's rule stays): a new `HELLO` without a token is a new player.
9. **`REJECT_IN_PROGRESS` stays** for `allowHotJoin = false`, a stopped (desynced) round and a round past the history bound.
10. **The game's reconnect UX is the existing one**: the token is saved per host address; the "Disconnected" page's Back returns to the join form with the address filled and Connect presents the token. No new button.

**Deviations, and brief/code-facts corrections**

- The brief's "WELCOME_MIDMATCH streams the record in chunks" became TICKS (decision 1); its "joiner verifies its first live checkpoint hash" became host-side verification before the slot (decision 3); "the joiner picks a slot in the overlay" is the host's choice (decision 6).
- `code-facts.md`: header note, §8 (a long paragraph on the whole mechanism), the fast-forward numbers. The brief's "Memory about 1 byte per slot per tick" is about 7 bytes per tick on the wire (6 input bytes and an event count) and 32 in memory; 9,000 ticks is about 63 KB on the wire and 288 KB in memory, still nothing.
- 08's Outcome says the join page reads "join between rounds": that text is now only for hosts that refuse late joiners.

**Follow-ups**

- **Browser reconnect, after 12 merges.** The token design is address-independent and a second loopback context stands in for a new vaddr in the tests, but nothing was run over the relay. Needed there: the token in browser storage (`NetPrefs` has the field; the file is native), a new relay context per attempt, the "sparse pumps" rule in 12 must not time a fast-forwarding tab out (the slices call `pump()`), and D's clamp (`MAX_DELAY`) decides who can be given a slot (`TAKEOVER_TOO_SLOW`). Test a hidden tab that reconnects.
- A spectator cannot become a player mid-round; a "take a bot" request after `JOIN_DONE` would be a small `READY_AT` variant. The lobby shows no list of spectators.
- The overlay toast for a returning player reads "X left - now AI" on the host (the old incarnation) and no "joined" (the id never left the list); a "rejoined" toast needs the host to say so.
- `evictDesynced` and a joiner that never reports are logged nowhere on the host's overlay.
- Untested glue (needs a display): `GameEngine::netJoinFrame`, the overlay drawing, pads in a join.

**Play-test** (pending; Arch Linux, three windows on one machine). Build as in brief 07/08 (RmlUi 4.3 from source, `git clone --recurse-submodules --branch netplay/13-hot-join ...` or the `claude` branch once merged), then from the build directory:

```bash
# three saved-settings folders so the windows do not share a token file
XDG_DATA_HOME=/tmp/alice ./BitRiot --host 7777 --players 4 --name Alice      # terminal 1
XDG_DATA_HOME=/tmp/bob   ./BitRiot --join 127.0.0.1:7777 --name Bob          # terminal 2
```

(Drag the windows apart; click a window before typing in it.)

1. **Start.** In Alice's window press Return: a round starts with Alice (player 1), Bob (player 2) and two bots. Play a minute, and let the bots wander.
2. **Join mid-round.** Terminal 3: `XDG_DATA_HOME=/tmp/carol ./BitRiot --join 127.0.0.1:7777 --name Carol`. Carol's window opens straight into the running round, shows "Joining... catching up N%" for a moment (instant on one machine; add `VNET_IMPAIR="lat=60,loss=5,seed=3"` in front of the command to watch it), then "You are player 3": it is a live bot of yours now (WASD + Left Ctrl/Left Shift in her window). Alice's and Bob's windows say "Carol joined" and the picture never hitches. All three pictures stay identical.
3. **No free bot / too far away.** Join a fourth window the same way (it gets the last bot), then a fifth: it reads "Watching - no player is free to take over" and shows the match but cannot play. `VNET_IMPAIR="lat=200,seed=4"` on a joiner gives "Watching - your connection is too slow for this round's input delay". `--spectate` always watches.
4. **Leave.** Press Esc in Carol's window: she is back at the main menu, her player carries on as a bot, and the others read "Carol left - now AI".
5. **Reconnect.** Start Carol again with the same command (or, to simulate a crash, `kill -9` her process first, then start it): she gets "You are player 3" again, the same slot, while the others see no stall. Check `cat /tmp/carol/BitRiot/BitRiot/netplay.txt` holds `token=` and `token_host=`.
6. **The scoreboard.** Let the round end: the late joiner's scoreboard and the others' agree; a window opened on the scoreboard joins the next round like any other.
7. **Records.** Run all three with `--record /tmp/rec-alice` and so on: after a round the three `round-*.brmr` files are byte-identical (`cmp`), also Carol's, which stepped from tick 1.
8. **Regression.** `./BitRiot` with no flags, `--host`, `--join`, the host and join menus, ready flags and `bitriot_headless host|join` behave as before.

Record the result here when it comes back.

**Review fix round** (an Opus review of cafdd7e: merge after fixes; new commits on top, no history change)

Claims corrected in the text above: the catch-up window is **1,000 ticks past the acknowledgement** (7 datagrams, up to about 1,150 in flight), not 1,100; "no sound" during the fast-forward is now true (it was not: see S1); `kick()` "in a round" was only true when the member matched no HELLO (S5).

- **B1 (reconnect got the wrong keyboard set).** `setupRound(joining)` built the late joiner's local mask from START's round-start owners, so a reconnect with its old peer id had its old slot local at once, WASD was bound to it, and `grantSlot` at the takeover moved it to the arrows. A late joiner now starts with local mask 0; its slots come only from `TAKEOVER_AT`. Tests: `Reconnect.APeerThatComesBack...` asserts `round.localSlotMask == 0`; `InputControlTest.AReconnectedJoinerStartsWithNoLocalSlot...` (a handler with mask 0, `grantSlot` gets WASD + Left Ctrl). Killed by: the mask built from START again; `grantSlot` starting at set 1.
- **S1 (fast-forward played sound).** `SoundMute` (`src/SoundMute.h`) installs a silent `MickBaseSound` around `fastForward` in `netJoinFrame` and puts the real backend back; no simulation source touched (the build id is unchanged). `SoundMuteTest`: silent during the scope, backend restored after. Killed by: the destructor not restoring.
- **S2 (phantom takeovers while waiting or watching).** `submitLocal`'s early return now reports a `HUMAN_TAKES_OVER` of a watcher or not-yet-participating joiner as not granted (`takeRejectedClaims()`, so the game's `revokeSlot` undoes the binding). Test `ASpectatorOrWaitingJoinerWhoClaimsABotIsToldItWasNotGranted`. Killed by: the loop removed.
- **S3 (back to the lobby with a joiner catching up).** A RUNNING late joiner or spectator that receives a `LOBBY` of the next round's number leaves the round and follows (the game shows the lobby page). Test `ALateJoinerStillCatchingUpFollowsTheHostBackToTheLobbyAndTheReadyGateOpens` (requireReady on). Killed by: the RUNNING case removed.
- **S4 (same token, two live machines).** A token from another address counts only once its old owner has been silent 1 s; a live owner makes the HELLO a new player after 1.5 s. Tests `TwoLiveMachinesWithTheSameToken...` and `ATokenFromADeadMachineIsHonouredOnceItHasBeenQuietForASecond` (plus every earlier reconnect test, which freezes the old machine). Killed by: the token always counting (both tests). Cost: a crash-restart now reclaims after about 1 s, not at once.
- **S5 (removed machine back in mid-round).** The `m_removed` (address, nonce) check runs before the member lookup. Test `AMachineRemovedInTheMiddleOfARoundWhoseWelcomeWasLostIsTurnedAwayToo`. Killed by: the check only without a member.
- **S6 (spectator's desync).** A mismatch of a `spectator` evicts only it. Test `ASpectatorWhoseStateIsWrongIsStoppedAlone`. Killed by: `midJoin` only.
- **S7 (test gaps).** (a) `ABotReservedForAJoinerThatVanishedBeforeItsTickIsFreeAgain` kills `!m.gone` removed from `reservedSlots`; (b) `TheTakeoverTickIsFarEnoughAheadThatTheLiveMatchDoesNotStallAtIt` asserts the tick is at least head + D + 2 and the live gaps stay under 70 ms; it kills `Ts = head + 1` together with (a). Under latency the stall itself is hidden by the backfill of neutral records, so the assertion is on the tick; (c) `TheRecordIsStreamedInAWindowNotAllAtOnce` with a new host statistic `maxCatchUpInFlight` (the window constant went from 1,100 to 1,000 so that it, and not the 8-datagram cap, binds: 1,071 in flight against 1,224 without it); (d) `ASpectatorDoesNotUseUpASlotOfTheSixWhenAnotherMachineAsksToJoin`.
- **Nits.** The unreachable "defer the takeover to t+1" branch is deleted; scheduled takeovers are never refused and are counted out of the peers' event share (18 minus them) so a tick still holds at most 24. R3's 8 s wait exempts slotless late joiners like `canStartRound` (test `ALateJoinerThatNeverFinishesCatchingUp...`; killed by removing the exemption). `READY_AT` round 0 does not decode. A late joiner the slots ran out for becomes a spectator at the next `START` (documented in protocol.md; test `ALateJoinerThatTheSlotsRanOutForWatchesTheNextRoundToo`). `GameEngine` refreshes `m_netSlotNames` from the lobby view once the joiner has caught up (glue, driven by hand only). The two test-only `SessionOptions` knobs are marked "TEST ONLY, never set by the game or the headless runner".
- **Skipped / survivors.** One mutation survives: removing `- scheduledTotal` from the peers' event share (`mergeTick`): reaching it needs a hostile peer flooding 18 events in the very tick of a scheduled takeover, which no test sets up (the cap is exercised by `AHostilePeerCannotFreezeTheOthers...`, which has no takeover in it). The earlier equivalent mutant (catch-up resend timer) stands.
- **Results.** GCC Release `bitriot_sim_tests` 583 (was 570), `bitriot_tests` 732 (was 718); Clang Release identical; `ctest` 3/3 and the process test 12 of 12 on both compilers, first run; `-DBITRIOT_NETPLAY=OFF` 414 and 531; web smoke 41 of 41; 200-seed sweep (GCC, tree not rebuilding, three at a time) 0 failing seeds on both binaries; build id still `0x6767612d056ba8af`.
