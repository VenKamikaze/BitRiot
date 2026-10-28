# 10 — vnet relay and WebSocket transport

- **Repo:** `VenKamikaze/vnet`
- **Branch:** per the vnet repo's convention
- **Depends on:** 06
- **Status:** todo

## Goal

Build a relay server, plus two client backends:

- a **browser** backend: WebSocket, written in JS for Emscripten
- a **native relay** backend: UDP to the relay

Together these let browser and native players share a "room" and exchange vnet datagrams as if they were on one virtual LAN. A game sees only the existing vnet API (06). It gets a virtual address and talks to other members' virtual addresses.

## Background (from the original design discussion)

- **Browsers have no raw UDP.** WebSockets are reliable and ordered (TCP), work everywhere, and are trivial to host. Under loss they stall (head-of-line blocking).
  - That is fine for lockstep games, which need every packet anyway. Cloudflare's Chocolate Doom used exactly this, with a message router.
  - Unreliable WebTransport datagrams come in brief 11.
- **Virtual addressing:** the relay assigns each member a virtual IPv4-style address in the room, for example `10.0.0.N`. Some games embed peer addresses in their own packets, so addresses must stay stable for a member's lifetime in the room.
- **Don't build an open proxy.** The relay only routes between members of the same room. A UDP *gateway* to arbitrary internet hosts is explicitly out of scope, and if it is ever added, it must use an allowlist.

## Design

- **Relay:** Go (D4). One static binary.
  - Listens on:
    - a WebSocket endpoint (`/vnet`), with binary frames, served over plain HTTP for local development and `wss://` in production behind TLS;
    - a UDP port for native clients.
  - Configured by flags and env.
  - Has a `Dockerfile`.
- **Framing:** identical over WebSocket frames and UDP datagrams.
  - A 1-byte message type, then the fields.
  - Control messages: `CREATE_ROOM` → `ROOM(code, your_vaddr)`, `JOIN_ROOM(code)` → `ROOM(...)` | `ERROR(reason)`, `LEAVE`, `KEEPALIVE`.
  - Data: `DATA(dst_vaddr, dst_port, src_port, payload)`. The relay fills in the source vaddr when forwarding.
  - Document the format in `doco/relay-protocol.md`, and version it in the first message.
- **Rooms**
  - Codes are short, human-typeable and unambiguous, for example 6 characters with no 0/O/1/I.
  - The creator gets `.1`, so a game can treat "host = `10.0.0.1`" as a convention. Document this; Bit Riot's brief 12 relies on it.
  - Configurable member cap (default 8).
  - A room dies when empty, or after an idle timeout.
- **Native relay identity:** native clients over UDP are identified by a session token issued in `ROOM`, not by their source IP:port, so a NAT rebinding doesn't break them. Keepalive interval: about 5 s.
- **Limits:** max payload `VNET_MAX_DATAGRAM`; per-client packet-rate and byte-rate limits; a room cap; a cap on total rooms.
- **Client backends**
  - **`VNET_BACKEND_RELAY_WS`** (browser): an Emscripten `--js-library` (`vnet_ws.js`) plus C glue.
    - It queues incoming frames in JS.
    - `vnet_poll()` moves them into a C ring buffer, and `vnet_recvfrom` reads that buffer, so it never blocks.
    - Connection and room setup are asynchronous, which shows as a status the game can poll (`vnet_status()`: CONNECTING, IN_ROOM, ERROR). Add that to the API carefully, and keep it generic.
  - **`VNET_BACKEND_RELAY_UDP`** (native): the same framing over a UDP socket to the relay. It lets native and browser players share rooms.
  - **Room selection:** `vnet_config` carries the relay URL or address and either "create room" or "join code". `vnet_resolve("room-host")`, or similar, returns the host vaddr.

## In scope

- The relay, both client backends, and the docs.
- A Python or Node CLI test client is fine for debugging.
- A `docker run` example and a short deployment note covering TLS termination (Caddy or nginx in front, `wss://`).

## Out of scope

- WebTransport (11).
- Authentication and accounts.
- Matchmaking and room listing. A room listing could come later.
- The UDP gateway.

## Tests

- **Go unit tests:** room lifecycle, address assignment, routing only within a room, limits, malformed frames rejected, and timeouts.
- **Native integration:** two native C clients on `RELAY_UDP` exchange datagrams through a locally running relay, with an impairment layer optionally in front.
- **Browser integration** with Playwright (Chromium is pre-installed):
  - a test page built with Emscripten, from a vnet example program, joins a room created by a native client;
  - they exchange N datagrams both ways;
  - assert content and order.
- **Load sanity:** one relay process with 50 rooms × 4 clients at 30 packets per second each. Report CPU and memory.

## Acceptance criteria

- One relay binary serves both WebSocket and UDP clients.
- Browser ↔ native exchange works through it.
- Limits are enforced and tested.
- The protocol is documented.
- No path forwards traffic to non-members.

## Outcome

*(fill in when done)*
