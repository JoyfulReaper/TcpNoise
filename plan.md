# tcpnoise plan

`tcpnoise` is a small POSIX C program for observing unsolicited TCP connection noise on public ports.

It started as a socket exercise and has turned into a useful little Internet-noise observer. Keep it understandable, portable between FreeBSD and Linux, and fun to work on.

The goal is **not** to turn tcpnoise into a packet analyzer, IDS, giant honeypot framework, or general networking platform.

---

# Working style

Continue in small slices:

1. Implement one understandable change.
2. Build and exercise it.
3. Review correctness, portability, ownership, cleanup, and error handling.
4. Fix problems before layering more behavior on top.
5. Prefer correct C/POSIX behavior over shortcuts.
6. Let real Internet traffic justify complexity.

---

# Current status

As of 2026-09-06, tcpnoise is running successfully against real public traffic on the Ubuntu VPS and has also been exercised on FreeBSD.

Working now:

- multiple TCP ports in one process;
- explicit IPv4 and IPv6 listeners;
- `IPV6_V6ONLY`;
- `poll()` across listeners;
- remote IP and source-port reporting;
- per-port current-run counters;
- SQLite-backed persistent seen-IP counts;
- first/last-seen timestamps;
- persistent per-port/IP-version activity;
- a 4096-byte bounded initial payload buffer;
- receive states for data, close, timeout, interrupted, and error;
- short accepted-client receive timeout;
- graceful `SIGINT` / `SIGTERM`;
- `SIGPIPE` protection;
- optional per-port randomized banners;
- safe partial banner writes;
- per-port text logs;
- local timestamps for console/logs and UTC for SQLite;
- separate banner, logging, networking, and database modules;
- `check_stats.sh` showing port activity, Hall of Shame, top IPs, recent IPs, and IPv6 sightings;
- real traffic observed on ports including 23, 80, 443, 8080, 2222, 2323, and 3389.

SQLite is now authoritative for persistent seen counts and persistent port activity.

The synchronous accepted-client path is still intentionally simple. Real traffic has not justified a full event-driven client state machine.

IPv6 listeners are active, but unsolicited IPv6 traffic has not yet appeared in the observed VPS sample.

---

# Immediate next goal

> **Publish each connection to NATS and make a desktop computer click like a Geiger counter whenever the Internet touches the VPS.**

First NATS slice:

1. Add the NATS C client dependency.
2. Add a small `messaging.c` / `messaging.h` module.
3. Connect to NATS once during startup.
4. Publish one bounded event to `tcpnoise.connection`.
5. Keep raw captured payload out of the first event.
6. Treat NATS as best effort: failure must never stop tcpnoise.
7. Clean up NATS resources on shutdown.
8. Write a tiny desktop subscriber that plays one click per live event.
9. Keep the click subscriber non-durable so reconnecting does not replay a backlog.
10. Optionally configure JetStream to capture the same subject for history and Mission Control.

Initial event shape:

```json
{
  "listenPort": 23,
  "addressFamily": "ipv4",
  "remoteAddress": "164.92.115.22",
  "remotePort": 36070,
  "seenCount": 98,
  "timestampUtc": "2026-09-06 03:15:42"
}
```

Possible later fields:

- receive result;
- bytes received;
- banner result;
- probe classification.

Absolute rule:

> Messaging failure must never interfere with connection collection or SQLite persistence.

---

# Architecture

Current:

```text
Internet
   |
   v
tcpnoise
   |
   +--> terminal
   +--> per-port logs
   +--> SQLite seen_ip
   +--> SQLite port_activity
```

Next:

```text
                         tcpnoise.connection
                                 |
                    +------------+-------------+
                    |                          |
                    v                          v
           live Core NATS subscriber      JetStream
                    |                          |
                    v                          +--> Mission Control
           desktop Geiger click               |
                                               +--> future .NET API
                                               |
                                               +--> other experiments
```

tcpnoise should remain the collector.

A future HTTP API belongs in a separate .NET service, not in the C collector.

---

# Database

Current `seen_ip` table:

```text
address        TEXT PRIMARY KEY
first_seen_utc TEXT NOT NULL
last_seen_utc  TEXT NOT NULL
seen_count     INTEGER NOT NULL
```

Current `port_activity` table:

```text
port             INTEGER
ip_version       INTEGER
first_seen_utc   TEXT
last_seen_utc    TEXT
connection_count INTEGER

PRIMARY KEY (port, ip_version)
```

## Next database relationship

Add source-IP -> destination-port activity so tcpnoise can answer:

```text
164.92.115.22
  port 23    92 hits
  port 2323   5 hits
```

Likely shape:

```text
ip_port_activity
----------------
address
port
ip_version
first_seen_utc
last_seen_utc
connection_count

PRIMARY KEY (address, port, ip_version)
```

Keep `seen_ip`; it still provides useful global totals.

---

# Lightweight dashboard

`check_stats.sh` is already useful enough to count as a dashboard.

Current views:

- persistent port/IP-version leaderboard;
- Hall of Shame top IP;
- top 10 most-seen IPs;
- top 10 most recently seen IPs;
- quick IPv6 log check.

Do not replace this with ncurses until the shell version actually becomes limiting.

---

# NATS / Geiger counter phase — NEXT

Start with one subject:

```text
tcpnoise.connection
```

Do not invent a large subject hierarchy yet.

Possible module:

```text
include/messaging.h
src/messaging.c
```

Possible public surface:

```c
bool messaging_initialize(...);
bool messaging_publish_connection(...);
void messaging_close(...);
```

Exact ownership should follow the NATS C library's actual rules.

Failure policy:

```text
NATS available       -> publish event
NATS unavailable     -> warn if useful, continue
publish fails        -> continue
JetStream unavailable-> continue
```

No synchronous retry loop in the accepted-client path.

The desktop clicker should use live Core NATS semantics.

JetStream can separately retain the same subject for consumers that actually want history.

Optional future ridiculousness:

- different sound/pitch by destination port;
- rate-limit audio during scanner bursts;
- angry clunk for 3389;
- special noise for 42069 if it ever gets hit.

---

# Mission Control — PLANNED

Prefer:

```text
tcpnoise -> NATS / JetStream -> Mission Control
```

over teaching tcpnoise to make Mission-Control-specific HTTP calls.

This keeps the collector independent of downstream implementation and availability.

---

# Future .NET API — OPTIONAL

If an API becomes useful:

```text
NATS / JetStream
      |
      v
TcpNoise.Api (.NET)
      |
      +--> GET /api/ports
      +--> GET /api/ips/top
      +--> GET /api/ips/recent
      +--> GET /api/ip/{address}
```

Benefits:

- tcpnoise stays small;
- API crashes do not affect collection;
- auth/TLS/versioning live in a framework suited for them;
- NATS becomes the boundary between collection and presentation.

---

# Payload capture — PLANNED

The buffer is currently 4096 bytes, but one larger `recv()` still does not guarantee a complete logical request.

TCP has no message boundaries.

Next improvement:

- bounded multi-read;
- hard total byte limit;
- hard deadline;
- correct `EINTR` handling;
- clean stop on peer close.

For HTTP-like traffic, optionally:

- read complete headers;
- inspect `Content-Length`;
- capture only a small bounded body;
- recognize chunked framing only if worthwhile.

Do not turn tcpnoise into an HTTP server.

---

# Scanner/probe classification — PLANNED

Possible classifications:

- HTTP;
- TLS ClientHello;
- SSH;
- Telnet-like;
- login/bot probe;
- empty/banner-waiting;
- unknown text;
- unknown binary.

Rules:

- explicit byte lengths;
- never assume NUL termination;
- conservative classification;
- unknown is valid;
- classification failure must never affect connection handling.

Real examples have already included plain HTTP on 443 and a TLS ClientHello-like probe on 3389.

---

# Optional TLS termination — LONG TERM

For TLS-like probes, tcpnoise may eventually terminate TLS using a real library such as OpenSSL.

Possible goals:

- ClientHello metadata;
- SNI;
- ALPN;
- bounded server-side handshake;
- decrypted application payload after handshake.

Never implement TLS primitives manually.

Keep byte/time limits strict.

---

# Limited protocol interaction — LONG TERM

Some scanners react to banners.

A future bounded interaction mode could do tiny scripted exchanges such as:

```text
server: login:
client: admin
server: Password:
client: admin
server: Login incorrect
```

Rules:

- strict maximum steps;
- strict byte/time limits;
- no command execution;
- no attacker-triggered outbound connections;
- deterministic fake responses;
- do not become a general honeypot framework.

---

# Personal Gopherhole output — OPTIONAL

The personal Gopherhole may expose sanitized pages such as:

```text
Today's Internet Garbage on Port 21
Today's Internet Garbage on Port 23
Today's Internet Garbage on Port 443
```

Before publishing:

- escape control/ANSI sequences;
- cap payload lengths;
- redact credentials, authorization headers, cookies, and obvious secrets;
- consider masking source IPs;
- never interpret scanner content as Gopher control data.

This belongs to the personal Gopherhole/content layer, not HappyGopher core.

---

# Cleanup / hardening backlog

## Source cleanup

Remove obsolete in-memory seen-IP files when convenient:

```text
include/seen.h
src/seen.c
```

## Argument parsing

- set `errno = 0` before `strtol()`;
- detect `ERANGE`;
- add real option parsing only when flags justify it.

## Timestamp handling

Later derive local and UTC strings from the same `time_t`.

Consider `localtime_r()` / `gmtime_r()` where appropriate.

## SQLite

- check `sqlite3_finalize()` results consistently;
- define migration/version behavior as schema grows;
- decide runtime DB failure policy;
- optionally make DB path configurable;
- consider WAL only when concurrent readers justify it.

## Logging

Audit:

- `fprintf()`;
- `fputc()`;
- `fflush()`;
- `fclose()`.

Use OS log rotation rather than building it into tcpnoise.

## Messaging

When NATS lands:

- understand connection ownership;
- clean up every resource;
- bound message size;
- correctly JSON-escape strings;
- never log credentials;
- rely on library reconnect behavior rather than inventing a retry framework.

---

# Review checklist

## Core networking

- [x] Multiple ports
- [x] IPv4 listeners
- [x] IPv6 listeners
- [x] `IPV6_V6ONLY`
- [x] `poll()`
- [x] Remote address/source port
- [x] Graceful signals
- [x] Safe accepted-socket cleanup
- [x] FreeBSD testing
- [x] Ubuntu VPS testing
- [x] Real public IPv4 traffic
- [ ] Observe real unsolicited IPv6 traffic

## Persistence / stats

- [x] Persistent seen-IP count
- [x] First/last seen
- [x] Persistent port/IP-version activity
- [x] Hall of Shame
- [x] Top IPs
- [x] Recent IPs
- [ ] Source-IP -> destination-port activity
- [ ] Schema migration/version strategy
- [ ] Finalize-result hardening

## Payload

- [x] Explicit length
- [x] Escaped display
- [x] 4096-byte bounded buffer
- [ ] Bounded multi-read
- [ ] Optional bounded HTTP body capture

## NATS / Geiger counter

- [ ] Add NATS C dependency
- [ ] Add messaging module
- [ ] Connect once at startup
- [ ] Publish `tcpnoise.connection`
- [ ] Make NATS-unavailable nonfatal
- [ ] Make publish failure nonfatal
- [ ] Clean shutdown
- [ ] Desktop live subscriber
- [ ] Play Geiger click
- [ ] Optional per-port sounds
- [ ] Optional burst-rate handling
- [ ] Optional JetStream capture

## Downstream

- [ ] Mission Control JetStream consumer
- [ ] Optional .NET API
- [ ] Keep all downstream failures isolated from tcpnoise

---

# Definition of core done

The core tcpnoise project is already substantially proven.

One binary can run like:

```sh
./tcpnoise 23 80 443 2222 2323 3389 8080
```

and:

- bind multiple ports;
- support IPv4 and IPv6 listeners;
- accept and close connections correctly;
- report remote endpoints;
- persist seen-IP history;
- persist port/IP-version activity;
- capture bounded payload data;
- send optional banners;
- write per-port logs;
- shut down cleanly;
- run on FreeBSD and Linux.

The next work is no longer about proving tcpnoise can function.

It is about making the little program increasingly interesting without wrecking its simplicity.

The immediate next extension is:

> **Publish each connection to NATS and make a desktop computer click like a Geiger counter whenever the Internet touches the VPS.**

After that, let the garbage decide what deserves to exist next.
