# Deployment and Detection Surface

How to stand myC2 up outside a loopback lab, what the current build leaks, and
what has to be implemented before any of that is worth doing.

Scope: this is lab and authorized-testing infrastructure. The measurements below
come from the code in this repository and from a live run against `build/server`
on loopback, and are marked as such. Anything unverified is labelled.

See `README.md` for building, running, and the wire protocol. This document is
about the deployment plane and the gaps.

---

## 1. The model, as built

```
   AGENT (victim)                              SERVER (teamserver)
   ─────────────                               ──────────────────
   outbound TCP ───────────────────────────▶   listen() on 0.0.0.0:8443
   random ephemeral source port                accepts, replies, closes
   never binds a port                          never dials the agent
   no state between beacons                    queue + exit flag in memory
```

The agent polls. The server can only answer. Every consequence that follows
comes from that direction of travel:

| property | why |
| --- | --- |
| nothing listens on the victim | no inbound socket to find, nothing in `LISTEN` |
| works through NAT / egress-only firewalls | the connection originates inside |
| no long-lived session to notice | one TLS connection per check-in, closed immediately |
| module survives a server restart | the next beacon just succeeds |
| **commands are never pushed** | they wait in a queue for the next poll |

The cost of the last row: a command reaches the agent up to one beacon interval
late (one poll interval, 3–7s in `src/implant/agent.c`), and its output is back
on the console roughly one interval after that.

## 2. Two planes, not one

The single most useful structural decision is to keep these apart:

| plane | who | port | exposure |
| --- | --- | --- | --- |
| **beacon plane** | victim → server | 8443 | must be reachable from the internet |
| **operator plane** | you → server | ssh / VPN | must never be reachable from the internet |

They are different trust domains. The beacon plane is untrusted by definition —
anything on the internet can reach it. The operator plane is the opposite. Any
design that puts them on the same interface with the same controls is wrong.

## 3. What this build leaks today

Measured against `build/server` on loopback unless noted. Ordered by how easily a
defender picks it up.

| signal | current state | source |
| --- | --- | --- |
| **Certificate chain** | self-signed: `issuer == subject`, one-element chain, `CN=localhost` + IP SANs. Trivially an IOC to generate. | `src/server/CMakeLists.txt` |
| **Cadence** | 3–7s, jittered from a 5s base. The even beat is gone; a fixed destination on a schedule is still a pattern. | `agent.c` `POLL_BASE_MS` / `POLL_JITTER_PERCENT` |
| **Port** | 8443 fixed, and unusual in egress logs — ordinary traffic is overwhelmingly 443. | `beaconEndpoints.hpp` `kPort` |
| **Path** | `/api/v2/metrics` and `/api/v2/events`, defined once in `src/protocol.h`. Telemetry-shaped, but still fixed strings. | both |
| **Artifact metadata** | stripped: no `.symtab`, no build id, no `.comment`, no string naming the role or the paths. Nothing left to triage from the file itself. **Verified** with `readelf`/`nm`/`strings`. | `src/implant/CMakeLists.txt` |
| **Request shape** | `POST`, `Content-Type: application/octet-stream`, **no `User-Agent`**, no `Accept*`, no `Accept-Encoding`. A client with no UA at all is itself abnormal. | `https_client.c` `https_post` |
| **Response shape** | every response carries `Keep-Alive: timeout=5, max=100` (httplib's default idle timeout). **Verified** live; there is *no* `Server:` header. | httplib |
| **Body** | 12-byte binary header where byte 4 is `0x64`. Short, high-entropy-ish, unrecognised. | `src/protocol.h` |
| **`Host` header** | omits the port (`Host: 1.2.3.4`, not `1.2.3.4:8443`), which is unusual for a non-default port. | `https_client.c` |
| **Teamserver IP** | a fixed public address dialled on a schedule. | — |

What is *not* a leak: the bodies themselves. TLS covers them; a network sensor
sees opaque records either way.

### The one that matters most

The self-signed chain. It is the cheapest thing for any inline sensor or proxy to
flag, and it is the reason the certificate discussion is not cosmetic.

### A trust bug, verified by reading

`https_client.c` does this:

```c
SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
(void)SSL_CTX_set_default_verify_paths(ctx);          /* loads the system store */
if (ca_file != NULL && SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1) ...
```

`load_verify_locations` **adds** trust anchors; it does not replace the system
store loaded by `set_default_verify_paths`. So trust is the **union** of the
pinned file and every public root on the victim.

Consequence: the "pinning" described in the README is not exclusive. On a host
with a corporate TLS-inspection root installed (most managed endpoints), a proxy
presenting a forged certificate for the C2 hostname will **pass** verification.
The same applies to any publicly-trusted certificate for that name.

For a bare IP target this is mostly theoretical, because public CAs do not
generally issue IP certificates. For a DNS name it is a real hole. Fix: drop the
`set_default_verify_paths` call when a pinned CA is supplied.

Related: the SAN is covered by one `SERVER_IP` and one `SERVER_HOST`
(`src/server/CMakeLists.txt`). One certificate therefore cannot name both the
public address victims dial and the VPN address an operator dials.

## 4. Topology: disposable VM + VPN

The right first shape, and what it does and does not buy:

| | |
| --- | --- |
| ✅ your real IP never touches the C2 box | yes |
| ✅ the box becomes disposable — rebuild or rotate it without changing your access path | yes |
| ✅ operator traffic and beacon traffic on separate interfaces, so logs do not correlate | yes |
| ❌ the victim → server traffic | unchanged. This is all a defender ever sees. |
| ❌ the hostname, the chain, the cadence | unchanged |
| ❌ the VM's public IP | unchanged — it must be reachable or there is no C2 |

**The trap:** if the VM is reachable *only* over the VPN, victims cannot reach it
and there is no C2. The VPN hides the cockpit, not the plane.

Two ways to use it, with different requirements:

- **VPN for SSH, console runs on the VM.** Works today, no code changes. The
  certificate only needs to name the address the *victim* dials.
- **VPN for an operator API over HTTPS.** Needs the SAN to cover the VPN address
  *as well as* the public one, which needs a multi-IP SAN. Not supported today.

### Three gaps this topology exposes in the current code

1. **`server.listen("0.0.0.0", kPort)` binds every interface**, including the VPN
   one (`src/server/server.cpp`). Both endpoints are therefore reachable from
   inside the VPN as well as from the internet, and neither authenticates.
2. **No authentication.** `/api/v2/metrics` calls `take_pending()`, which
   **pops** the queue — so anyone who can reach that port can poll and *steal
   your queued command*; the real agent then finds an empty queue. Anyone can
   also POST a well-formed frame to `/api/v2/events` and put arbitrary text on
   your console
   (validated only for magic and length).
3. **The check-in frame is never validated.** `/api/v2/metrics` discards the body
   entirely (`(void)req;` in `beaconEndpoints.cpp`), so any body at all is
   accepted as a check-in. Confirmed live: a hand-built 12-byte body returned
   `204`. There is no per-victim identity anywhere — the server cannot tell two
   implants apart.

## 5. Certificate strategy

Pin the **root**, not the leaf.

- **Leaf pinning** is the most restrictive — only that exact certificate is
  accepted — but the certificate's renewal, or a replaced VM, silently kills
  every implant in the field, and those are the ones you cannot rebuild.
- **Root pinning** survives renewal and host rotation. The cost is that anything
  else that root signs is also trusted. Acceptable when you own the CA.

Order of preference for a lab that outlives a single session:

1. **Private CA** you control: root + short-lived leaf. No domain needed, IP SANs
   work, no ACME, no inbound port 80. Removes the one-element-chain IOC and makes
   the leaf rotatable.
2. **Public CA** on a domain you own. A chain any observer accepts. Costs: the
   name becomes discoverable in DNS, issuance needs HTTP-01 (port 80) or DNS-01,
   and the leaf rotates every ~90 days, which is only survivable if you pinned
   the root.
3. **Fronted behind a legitimate host.** Strongest and most setup.

Server side, the certificate paths are **compile-time constants relative to the
working directory** (`src/server/server.cpp`: `kCertPath` / `kKeyPath`). Dropping
a real fullchain at `certs/server.pem` (leaf **+ intermediate** concatenated, not
the leaf alone) works with no code change.

Watch the build rule: the `add_custom_command` in `src/server/CMakeLists.txt`
names `certs/server.pem` as an OUTPUT and regenerates a **self-signed** pair
whenever it is missing. Delete `certs/` with a real certificate installed and the
next build quietly replaces it with a self-signed one.

## 6. Implementation backlog

Ordered by what unblocks deployment, not by difficulty.

### Blocks any real use

1. **Per-session identity.** A session ID in the check-in, and the queue keyed by
   it. Fixes "two implants interleave each other's commands" and the sticky
   `exit` (`g_exit_requested` is never cleared), and is a prerequisite for
   everything else. `beaconEndpoints.cpp`.
2. **Authentication on both endpoints.** A shared secret in a header, or a pinned
   client certificate. Without it the queue is drainable by anyone who can reach
   the port, and the console is writable by anyone.
3. **Configurable bind address.** Replace `0.0.0.0` with an explicit interface so
   the beacon plane and operator plane can be separated. `server.cpp`.
4. **Persistent command queue.** Move `g_queue` / `g_exit_requested` out of
   process memory into a file, SQLite, or Redis. Required to survive a restart,
   and mandatory for any deployment behind more than one process.

### Required to host it anywhere but a loopback box

5. **Operator API.** `std::getline(std::cin)` cannot be driven remotely. Replace
   the console with authenticated HTTP endpoints to queue commands and read
   results, bound to the VPN interface only.
6. **Exclusive trust anchor.** Drop `set_default_verify_paths` when a pinned CA
   is given, so verification means "this CA or nothing".
7. **Multi-IP SAN.** Accept a list in `SERVER_IP` so one certificate can name the
   public address and the VPN address.

### Reduce the surface

8. **Plausible request shape.** Set a `User-Agent` consistent with the cover
   traffic, send `Accept*` headers, include the port in `Host`. Do not strip
   everything — a headerless client is as distinctive as a wrong one.
9. **Response hygiene.** Override httplib's `Keep-Alive` so responses do not
    advertise a stock HTTP server's defaults.
10. **Path and port configurable**, so they are not constants compiled into both
    binaries.
11. **Validate the check-in** on `/api/v2/metrics` the way `/api/v2/events` is
    validated, so a malformed frame is rejected rather than treated as a
    check-in.

### Correctness, not stealth

12. **Binary-safe console output.** `/api/v2/events` writes raw bytes to
    `std::cout`
    after a text header; non-UTF-8 output corrupts the terminal. Base64 or
    hex-escape on the console.
13. **Network byte order.** `src/protocol.h` documents host byte order, which is
    correct only between same-endian hosts.
15. **Larger result channel.** `OUTPUT_MAX` is 4096 bytes and output is only
    collected after the command exits, so a long-running command produces
    nothing until it finishes.

## 7. Verifying a deployment

```sh
# 1. does the beacon plane answer at all?
curl -sk -i --http1.1 -X POST \
  --data-binary $'\x41\x41\x41\x41\x64\x00\x00\x00\x00\x00\x00\x00' \
  https://<host>:8443/api/v2/metrics

# 2. what does the chain look like to an observer?
openssl s_client -connect <host>:8443 -showcerts </dev/null 2>/dev/null \
  | openssl x509 -noout -subject -issuer -dates
#   issuer == subject  ->  one-element chain, still self-signed

# 3. is the beacon port reachable from a network you do not control?
#    (from outside, not from the VPN interface)

# 4. from inside the VPN: is /api/v2/metrics also answering there? It should
#    not be, until §6 item 3 and item 5 are done.
curl -sk -o /dev/null -w '%{http_code}\n' -X POST --data-binary 'x' \
  https://<vpn-address>:8443/api/v2/metrics
```

A `204` from step 4 means the beacon plane is reachable on the operator plane,
and that the unauthenticated queue drain in §4 is exposed to anything on the
VPN.

---

## Current state summary

Working and verified: clean configure and build, TLS with enforced verification,
framing validation on `/api/v2/events`, a 4-shared-library implant, reproducible
container build, build-time certificate generation with a configurable SAN, a
jittered 3–7s poll interval, and an implant binary with no symbol table, build id,
compiler version or role-shaped strings.

Not yet true: authentication, per-session state, a persistent queue, a remote
operator interface, root-pinned trust, or any configurability of port, path or
bind address. It is a working beacon channel; it is not deployable
infrastructure.
