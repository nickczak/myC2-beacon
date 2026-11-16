# Teamserver

The operator side. It listens on `0.0.0.0:8443`, hands the implant one queued
command per check-in, and prints the output that comes back.

```
   AGENT (victim)                              SERVER
   ─────────────                               ──────
   outbound TCP ───────────────────────────▶   listen() on 0.0.0.0:8443
   random ephemeral source port                accepts, replies, closes
   never binds a port                          never dials the agent
```

The server never initiates a connection. Everything it "sends" is a reply to a
request the implant made — which is why a queued command waits for the next
beacon instead of arriving immediately.

## Layout

| File | Contents |
| --- | --- |
| `server.cpp` | `main`, certificate loading, `listen`, request logging |
| `operatorConsole.cpp` | `sessions` / `use` / `exit` / `quit`, and queueing everything else |
| `serverOutput.cpp` | the single write path to stdout: messages, prompt, `hex_id` |
| `endpoints/beaconEndpoints.cpp` | the two request handlers |
| `endpoints/beaconEndpoints.hpp` | `kPort`, `kMaxPayload` |
| `endpoints/sessionStore.cpp` | per-session queues, the in-flight command, TTL sweep |
| `endpoints/sessionStore.hpp` | `Session`, `SessionInfo`, `Pending`, `SessionStore` |

`endpoints/` is compiled into the `server` target through
`target_sources`; its headers are added to the target's private include path.

## Building

The teamserver is built by default. It is the only half that needs
[cpp-httplib](https://github.com/yhirose/cpp-httplib), which is fetched rather
than vendored:

```cmake
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
    GIT_TAG v0.66.0                       # pinned; tested commit cdcd2ac...
    GIT_SHALLOW TRUE)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
```

The first configure clones it into `build/_deps`; later builds reuse that copy.
`HTTPLIB_REQUIRE_OPENSSL ON` makes the configure **fail** rather than quietly
produce a server with no HTTPS. On an offline or air-gapped machine, point CMake
at a local checkout instead:

```sh
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=/path/to/cpp-httplib
```

The implant does not need cpp-httplib, so the whole server can be dropped from a
build with `-DBUILD_SERVER=OFF`.

### Sanitizers

`-DENABLE_SANITIZERS=ON` adds address and undefined-behavior sanitizers to every
target. It needs the sanitizer runtime installed (`libasan`/`libubsan` on
Fedora); without it the configure stops with an explicit message instead of
failing later at link time with an opaque `cannot find libasan.so`.

Formatting preferences live in `.clang-format` and are **not** enforced by the
build — there is no lint target.

## Certificate

`certs/` is gitignored, so a fresh clone has none. The build generates a
self-signed pair into `certs/server.pem` and `certs/server.key` whenever either
file is missing (RSA 2048, valid 365 days, `CN=localhost`). An existing pair is
left alone unless the SAN stamp changes, so a plain rebuild cannot rotate the
trust anchor out from under a running implant.

The server loads both paths relative to the working directory, as compile-time
constants in `server.cpp`:

```cpp
constexpr const char *kCertPath = "certs/server.pem";
constexpr const char *kKeyPath  = "certs/server.key";
```

Run it from the project root or it will not find them. Degenerate files are
caught at startup: `SSLServer::is_valid()` fails and the process exits with code
1 and a message naming both paths.

### SAN

The SAN defaults to `DNS:localhost,IP:127.0.0.1`, which is enough for loopback
and nothing else. The implant pins the certificate **and** checks the name, so a
victim connecting to any other address is rejected by verification. Name the
deployment address when configuring:

```sh
cmake -S . -B build -DSERVER_IP=192.168.50.50
# or a hostname instead of an address
cmake -S . -B build -DSERVER_HOST=c2.lab
```

| Option | Effect |
| --- | --- |
| `SERVER_IP` | appends `IP:<value>` to the SAN |
| `SERVER_HOST` | appends `DNS:<value>` to the SAN |

Each accepts **one** value, and one certificate names both the loopback default
and the one deployment address. A certificate cannot cover a public address and a
VPN address at the same time without editing the CMake.

Changing either option regenerates the certificate on the next build; changing
neither leaves it alone. The mechanism is a `configure_file` stamp
(`cert_san.txt.in`) that the certificate rule depends on, and which is only
rewritten when the SAN string changes. To force new certificates anyway:

```sh
rm -rf certs && cmake --build build
```

A real certificate can be dropped in at these paths with no source change — but
name the leaf **and** its intermediate, concatenated. Note that the build rule
declares `certs/server.pem` as an OUTPUT, so if that file ever disappears the
build regenerates a self-signed pair in its place.

## Running

```sh
./build/server
```

```
[*] Linux C++ Teamserver listening on https://0.0.0.0:8443
c2_server>
```

The port is the compile-time constant `kPort` in `beaconEndpoints.hpp`. There is
no command-line argument to change it or the bind address; both are `0.0.0.0`
and `8443` as written. A bind failure prints `[-] Failed to bind 0.0.0.0:8443`
and exits with code 1, but a second instance is not one: cpp-httplib sets
`SO_REUSEPORT`, so two teamservers share 8443 instead of the second one failing.
Verified with two live instances both in `LISTEN`.

### Operator console

The port is bound before the console thread starts, so a genuine bind failure
returns before any thread is left blocked on stdin. After that a detached thread
reads `std::cin` while `listen` runs.

| input | effect |
| --- | --- |
| `sessions` | list every live session id, `*` marking the selected one |
| `use <hex id>` | send later commands to that session |
| `exit` | ask the selected session to shut down |
| `exit <hex id>` | ask that one session to shut down |
| `exit all` | ask every session to shut down |
| `quit` | stop reading input; the teamserver keeps serving |
| any other text | queued for the selected session |

Nothing is queued until a session is selected:

```
c2_server> id -u
[-] no session selected; run `sessions` then `use <id>`
c2_server> sessions
[*] 1 session(s), * = receives commands
    5f3a91c2  0s ago
c2_server> use deadbeef
[-] no session deadbeef; run `sessions`
c2_server> use 5f3a91c2
[*] using 5f3a91c2
c2_server> id -u
c2_server> [*] POST /api/v2/metrics -> 200

[+] result from 5f3a91c2 for `id -u` (5 bytes):
1000
[*] POST /api/v2/events -> 200
```

`exit` sets a per-session flag that is **never cleared**, so every later check-in
of that session is answered with `CMD_EXIT` until the teamserver is restarted.
Control bytes in a result print as `^X`, so a command cannot rewrite the
operator's terminal; bytes above 0x7f pass through, which keeps UTF-8 intact.

Every request is logged by `set_logger`, which is what produces the
`[*] POST /api/v2/metrics -> 204` lines — one per check-in, 3–7s apart (5s base,
±40% jitter). All console and handler output goes through `serverOutput.cpp`, so
a log line arriving mid-keystroke starts on a fresh line instead of landing
inside `c2_server> `; it still does not put back the line you were typing.

`quit` ends the console thread; `listen` keeps serving until the process is
stopped. A bare `exit` is not an alias for it: it targets the selected session,
and with none selected it prints the same `no session selected` error as any
other command.

## Endpoints

Both take a `C2_HEADER`-framed body; see the protocol section of the
[`root README`](../../README.md#the-protocol).

| Endpoint | Response |
| --- | --- |
| `POST /api/v2/metrics` | `200` + framed command for the calling session, `204` when its queue is empty, `400` for a malformed frame, `413` if a queued command exceeds the cap |
| `POST /api/v2/events` | `200`, `400` for a malformed frame, `413` above the cap |

Both paths come from `C2_PATH_CHECKIN` / `C2_PATH_RESULT` in `../common/protocol.h`, not
from literals here, so renaming one side renames both.

`kMaxPayload` (1 MiB) is also applied to the whole request by
`server.set_payload_max_length`.

### Validation

Both handlers call the shared `frame_decode`, so a body shorter than 16 bytes, a
wrong `magic`, a `session_id` of zero or a `payload_size` that does not account
for the rest of the body exactly is a `400` on either endpoint.

`/api/v2/metrics` registers the session the frame names and then serves *that*
session's queue. `/api/v2/events` acknowledges the command the upload answers,
but never creates a session.

### Sessions and queues

`SessionStore` is a `std::map<std::uint32_t, Session>` behind a mutex, one entry
per session id. A session is created by its first check-in and holds a
`std::deque` of queued commands, the command currently in flight, an
`exit_requested` flag and `last_seen`.

- `check_in`, `take`, `queue`, `request_exit` and `snapshot` all sweep first: a
  session silent for longer than the store's TTL is erased, and its queue with
  it. The TTL is 60s, the `SessionStore` constructor's default. The implant beacons every 3–7s, so a minute of quiet means it is gone
  rather than idle.
- `take` moves the front of the queue into `in_flight` and leaves it there until
  `/api/v2/events` acknowledges it. A check-in that arrives with that result
  still missing is answered with the same command again, which is what makes
  delivery at-least-once instead of lossy.
- A command is sent at most three times. A fourth check-in with no result drops
  it and prints `dropped`, so a client that checks in but never uploads cannot
  keep one command running forever.
- `acknowledge` also refreshes `last_seen`: a result is proof of life, and a
  command that ran longer than the TTL must not lose its session before its
  output lands.
- An unknown id is answered like an empty queue, so asking for work cannot
  create a session, and `queue` fails for one, which is what prints
  `session <id> is gone`.

Three consequences worth knowing before this leaves a lab:

- anything that can reach the port can claim a session id, take its queued
  command before the real implant does, and post well-formed frames to
  `/api/v2/events` to put arbitrary text on the console;
- at-least-once is not exactly-once: when a result upload fails the command is
  sent again, so a non-idempotent command can run more than once;
- the label printed with a result is whatever is in flight at that moment, since
  a result frame names only its session. A duplicate upload of an older result
  therefore prints unlabelled.

Both endpoints are unauthenticated. See [`../../DEPLOYMENT.md`](../../DEPLOYMENT.md).

## Limitations

- No authentication on either endpoint: a session id is claimed, not proven.
- State is in process memory only; a restart loses every queue and exit flag.
- Delivery is at-least-once with no idempotency: a failed upload re-runs the
  command, up to three sends.
- `exit` is sticky until restart, for a session that is still alive.
- Binds `0.0.0.0`, so the beacon endpoints are reachable on every interface,
  including a VPN's.
- Port and bind address are not configurable without editing the source.
- Certificate paths are relative to the working directory.
- Control bytes in a result are escaped, but nothing distinguishes a command
  that printed `^A` from one that printed a real `0x01`.
