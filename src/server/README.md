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
| `server.cpp` | `main`, the operator console, certificate loading, `listen` |
| `endpoints/beaconEndpoints.cpp` | the command queue and the two request handlers |
| `endpoints/beaconEndpoints.hpp` | `kPort`, `kMaxPayload`, the `Pending` enum, queue API |

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
file is missing (RSA 2048, valid 365 days, `CN=localhost`). **Existing
certificates are never overwritten**, so the trust anchor cannot rotate out from
under a running implant.

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
and `8443` as written. If the port is taken, the server prints
`[-] Failed to listen on port 8443` and exits.

### Operator console

A detached thread reads `std::cin` while `listen` blocks, so typing works from
the moment the server starts.

| input | effect |
| --- | --- |
| any other text | queued; handed to the implant on its next check-in |
| `exit` | asks the implant to shut down |

Output arrives as it is uploaded:

```
c2_server> id -u
[*] POST /api/v2/metrics -> 200

[+] result (5 bytes):
1000
[*] POST /api/v2/events -> 200
```

`exit` sets a flag that is **never cleared**, so every later check-in is answered
with `CMD_EXIT` until the server is restarted. The result printer writes raw
bytes to the terminal after a text header, so binary output will corrupt it.

Every request is logged by `set_logger`, which is what produces the
`[*] POST /api/v2/metrics -> 204` lines — one per check-in, five seconds apart on
average and jittered ±40%.

## Endpoints

Both take a `C2_HEADER`-framed body; see the protocol section of the
[`root README`](../../README.md#the-protocol).

| Endpoint | Response |
| --- | --- |
| `POST /api/v2/metrics` | `200` + framed command, `204` when the queue is empty, `413` if a queued command exceeds the cap |
| `POST /api/v2/events` | `200`, `400` for a malformed frame, `413` above the cap |

Both paths come from `C2_PATH_CHECKIN` / `C2_PATH_RESULT` in `../protocol.h`, not
from literals here, so renaming one side renames both.

`kMaxPayload` (1 MiB) is also applied to the whole request by
`server.set_payload_max_length`.

### Validation is asymmetric

`/api/v2/events` parses the frame: a body shorter than a header, a wrong `magic`,
or a `payload_size` that does not account for the rest of the body exactly is a
`400`. That is what rejects truncation, bad magic and oversized bodies.

`/api/v2/metrics` does not look at the body at all (`(void)req;`). Any body is accepted as
a check-in, which also means the server has no way to tell one implant from
another. There is no session identifier anywhere.

### The queue

`g_queue` is a `std::deque<std::string>` behind a mutex, and `take_pending()`
**pops** from it — the command is consumed by whichever check-in arrives next.
Two consequences worth knowing before this leaves a lab:

- two implants interleave each other's commands, since there is one queue;
- anything that can reach the port can poll `/api/v2/metrics` and take a queued
  command before the real implant does, and can post well-formed frames to
  `/api/v2/events` to put arbitrary text on the console.

Both endpoints are unauthenticated. See [`../../DEPLOYMENT.md`](../../DEPLOYMENT.md).

## Limitations

- One global queue, no session identity: implants interleave.
- `exit` is sticky until restart.
- No authentication on either endpoint.
- Binds `0.0.0.0`, so the beacon endpoints are reachable on every interface,
  including a VPN's.
- State is in process memory only; a restart loses the queue and the exit flag.
- Port and bind address are not configurable without editing the source.
- Certificate paths are relative to the working directory.
- Result output is written to the terminal as raw bytes.
