# 06 — vnet core: API, loopback and native UDP

- **Repo:** `VenKamikaze/vnet`. The user creates it (D3). Move this brief into that repo's `doco/` when work starts.
- **Branch:** `main` for the initial skeleton, or follow the same base/feature-branch convention if the user sets one up for vnet.
- **Depends on:** nothing. This can run in parallel with 01–05.
- **Status:** todo

## Goal

Build a small, dependency-free C library that gives games a non-blocking, datagram-style "virtual UDP" API. Backends sit underneath it.

This brief delivers:

- the **API**
- a **loopback** backend: in-process, deterministic, for tests
- a **native UDP** backend
- an **impairment** layer (latency, jitter, loss, duplication, reordering) that can wrap any backend

Briefs 10 and 11 add relay and browser backends without changing the API.

## Background

- Every target game already isolates its networking behind a small driver: Bit Riot's netplay layer (07), jfbuild's `mmulti.c`, Doom ports' `i_net.c`. vnet slots in under those drivers.
- Browsers have no UDP. They get WebSocket and WebTransport (briefs 10–11) through a relay. That is why the API is "virtual": a relay can assign addresses.
- Lockstep games need every packet but tolerate latency. Client/server games such as Odamex want unreliable datagrams. vnet itself is **unreliable datagrams only**, and reliability is the game protocol's job (D7). Keep it that way.

## API sketch

Refine it, but keep the shape: C11, a single public header `vnet.h`, and `extern "C"`-safe.

```c
typedef struct vnet_addr { uint8_t family; uint8_t bytes[16]; uint16_t port; } vnet_addr; // or opaque + accessors
int  vnet_init(const vnet_config *cfg);   // choose backend: VNET_BACKEND_UDP | _LOOPBACK (later _RELAY_*)
void vnet_shutdown(void);
int  vnet_open(uint16_t port);            // returns socket handle; port 0 = ephemeral
void vnet_close(int s);
int  vnet_sendto(int s, const void *buf, size_t len, const vnet_addr *to);       // never blocks
int  vnet_recvfrom(int s, void *buf, size_t cap, vnet_addr *from);              // never blocks; VNET_EAGAIN when empty
void vnet_poll(void);                     // pump the backend once per frame (needed by async/browser backends)
int  vnet_resolve(const char *host_port, vnet_addr *out);   // "1.2.3.4:5000", "[::1]:5000", later "room:ABCD"
int  vnet_addr_to_string(const vnet_addr *a, char *buf, size_t cap);
int  vnet_addr_equal(const vnet_addr *a, const vnet_addr *b);
const char *vnet_strerror(int err);
```

**Rules**

- **Max payload:** `VNET_MAX_DATAGRAM` = 1200 bytes, so a payload fits a WebTransport datagram later. Oversize sends fail with a clear error and are never truncated silently.
- **No blocking calls**, no threads, and no hidden allocation per packet in steady state (use a ring buffer).
- **No global state beyond the backend instance**, so that tests can init and shut down repeatedly.
- **Logging:** through a user-supplied callback, silent by default.

## In scope

1. **Repo skeleton:**
   - `CMakeLists.txt` with a `vnet` static library target.
   - Usable via `add_subdirectory`/`FetchContent` and via `find_package(vnet)` after install (export a config package).
   - `include/vnet.h`, `src/`, `tests/`, `README.md`, and `doco/`, with this brief moved there.
   - A licence: ask the user. A permissive licence such as zlib or MIT suits reuse by GPL game ports. Don't pick one without asking.
2. **Loopback backend:**
   - Multiple "hosts" in one process, each with its own virtual address (for example `127.1.0.N`).
   - Delivery happens on `vnet_poll()`, in a deterministic order, so that a test can run several peers in a single thread with exact repeatability.
3. **Native UDP backend:**
   - BSD sockets, non-blocking, IPv4 and IPv6.
   - Builds on Linux and macOS. Windows/Winsock should at least compile behind `#ifdef`; full Windows CI isn't required. Bit Riot has a MinGW build, so keep that path in mind.
4. **Impairment layer:**
   - Configurable latency, jitter, loss %, duplicate % and reorder %, using a seeded PRNG so results repeat.
   - It wraps any backend: on loopback it is used in unit tests, and on UDP it is used in multi-process tests in Bit Riot's brief 07.
   - Enable it through `vnet_config` and through environment variables (for example `VNET_IMPAIR="lat=80,jit=20,loss=5"`), so a game can be tested under bad conditions without code changes.
5. **Tests:**
   - GoogleTest, for consistency with Bit Riot.
   - Coverage:
     - send/recv on loopback and on UDP over 127.0.0.1
     - `EAGAIN` semantics
     - oversize rejection
     - address round-trip to string
     - impairment statistics within tolerance over N packets
     - deterministic impairment for a given seed
     - repeated init and shutdown
6. **CI:**
   - A GitHub Actions workflow that builds and tests on ubuntu-latest.
   - A macOS job is optional.
   - The cloud session can't watch Actions runs, so verify locally as well.

## Out of scope

- Relay, WebSocket and WebTransport (briefs 10 and 11).
- Reliability and ordering layers.
- Encryption.
- A POSIX socket shim, i.e. intercepting `socket()`/`sendto()`. Possibly later, for unmodified games.

## Acceptance criteria

- `vnet.h` is documented: every function, error code, threading and blocking guarantees, and max payload.
- Loopback, UDP and impairment are tested and green locally and in CI.
- A short `README.md` explains how to consume vnet from a CMake project, with a 20-line example of two sockets exchanging a datagram.
- Bit Riot can consume it, via a submodule or `FetchContent`. Verify by building a throwaway consumer.
  - Note that a **private** vnet repo needs credentials to fetch in Bit Riot's CI and cloud setup. Raise that with the user if the repo is private.

## Outcome

*(fill in when done)*
