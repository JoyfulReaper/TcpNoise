# tcpnoise plan

`tcpnoise` is a small POSIX C program for observing unsolicited TCP connection noise on public ports.

It began as a simple C/socket exercise and has grown into a useful little network-noise observer.

The project should remain understandable and portable between FreeBSD and Linux.

The goal is **not** to turn tcpnoise into a packet analyzer, IDS, full honeypot framework, protocol-emulation framework, or giant networking abstraction.

---

# Working style

Implementation should continue in small slices:

1. Implement one understandable change.
2. Build and exercise it.
3. Review correctness, portability, ownership, cleanup, and error handling.
4. Fix problems before layering more behavior on top.
5. Prefer correct C/POSIX behavior over shortcuts.
6. Do not add complexity merely because a more elaborate architecture is possible.

Keep each slice small enough to understand and test independently.

---

# Current status

As of 2026-09-05, tcpnoise is a functional multi-port TCP network-noise monitor.

Working now:

* multiple TCP ports in one process;
* explicit IPv4 and IPv6 listeners;
* `IPV6_V6ONLY` behavior for predictable dual-stack operation;
* `poll()` across listening sockets;
* remote IPv4/IPv6 address reporting;
* remote source-port reporting;
* per-port connection counters;
* bounded initial payload capture;
* distinct receive states;
* short accepted-client receive timeout;
* graceful `SIGINT` and `SIGTERM` shutdown;
* `SIGPIPE` protection;
* optional per-port banner pools;
* randomized banner selection;
* intentional chance of sending no banner;
* safe partial banner writes;
* `EINTR` handling during banner sends;
* per-port text logs;
* persistent log handles;
* log flushing after each event;
* SQLite-backed persistent seen-IP counts;
* first-seen and last-seen history;
* SQLite opened once for the process lifetime;
* parameterized SQLite statements;
* persistent seen counts across restarts;
* local timestamps for terminal/text-log display;
* UTC timestamps for SQLite persistence;
* separate banner, logging, networking, and database modules.

The old fixed-size in-memory seen-IP table is no longer part of the active runtime design.

SQLite is now authoritative for persistent seen counts.

The current synchronous accepted-client receive timeout remains approximately 250 ms.

Real public traffic has not yet demonstrated enough listener starvation to justify replacing the simple accepted-client path with a full event-driven client state machine.

---

# Immediate next goal

The immediate goal is:

> **Deploy the current implementation to the Ubuntu VPS and exercise the same source on Linux under real public traffic.**

Before or alongside deployment:

1. Remove the obsolete `seen.c` / `seen.h` files.
2. Build cleanly on FreeBSD.
3. Build cleanly on Ubuntu/Linux.
4. Run tcpnoise publicly on a nonprivileged test port such as `2222`.
5. Verify IPv4 traffic.
6. Verify IPv6 traffic.
7. Verify SQLite seen counts persist across process restarts.
8. Verify text logs continue to work.
9. Verify banners continue to work.
10. Verify clean shutdown closes SQLite, logs, listeners, and banner resources.

Do not add Mission Control, classification, or TUI work before the Linux deployment proves the current foundation.

---

# Goals

Core goals:

* Listen on one or more TCP ports from one process.
* Support IPv4 and IPv6 explicitly.
* Count accepted connections per configured port.
* Track how many times each remote IP has been seen.
* Preserve seen-IP history across restarts.
* Safely preview a bounded amount of initial payload data.
* Keep useful per-port text logs.
* Gracefully stop on signals.
* Optionally send shallow configured banners.
* Remain usable on FreeBSD and Linux.
* Keep dependencies deliberate and minimal.
* Remain understandable as a C/POSIX learning project.

Possible later goals:

* Mission Control telemetry;
* simple scanner/probe classification;
* SQLite connection-event history;
* rolling statistics;
* terminal history and per-port views.

Example usage:

```sh
./tcpnoise 23 80 2222 2323
```

Optional banner files:

```text
23_banner.txt
80_banner.txt
2222_banner.txt
2323_banner.txt
```

A missing banner file is normal and must not prevent a listener from starting.

---

# Non-goals

tcpnoise will not currently:

* capture raw SYN packets;
* observe connections that never complete a TCP handshake;
* implement full Telnet;
* implement full HTTP;
* implement SSH;
* implement TLS;
* implement FTP;
* become a general-purpose honeypot framework;
* retaliate against scanners;
* exploit remote systems;
* perform active counterattacks;
* implement its own TLS stack;
* implement its own HTTP client;
* implement its own database engine;
* implement log rotation internally;
* become multithreaded merely because threads exist.

An **accepted TCP connection** is the unit tcpnoise observes.

Banners remain intentionally shallow.

Sending:

```text
SSH-2.0-OpenSSH_9.9
```

is in scope.

Implementing an SSH server is not.

---

# Current architecture

Conceptually:

```text
command-line ports
       |
       v
per-port state
       |
       +--> banner pool
       +--> text log
       +--> IPv4 listener
       +--> IPv6 listener
       |
       v
     poll()
       |
       v
accept ready connection
       |
       +--> optionally choose/send banner
       |
       v
brief bounded recv()
       |
       v
resolve numeric remote endpoint
       |
       v
persistent SQLite seen-IP update
       |
       v
build/output connection information
       |
       +--> terminal
       +--> per-port text log
       |
       v
close accepted socket
```

SQLite is opened once during startup and reused throughout the process.

IPv4 and IPv6 use separate listener sockets rather than relying on IPv4-mapped IPv6 behavior.

Per-port IPv4 and IPv6 listeners share:

* the same banner pool;
* the same text log;
* the same per-port connection counter.

Seen counts are global by address rather than by listening port.

---

# Current source layout

Active source layout:

```text
07-tcpnoise/
├── Makefile
├── README.md
├── plan.md
├── banners.txt
├── include/
│   ├── banner.h
│   ├── database.h
│   ├── logging.h
│   └── network.h
└── src/
    ├── main.c
    ├── banner.c
    ├── database.c
    ├── logging.c
    └── network.c
```

`main.c` remains responsible for orchestration and the main event loop.

Do not split every tiny helper into its own `.c`/`.h` pair simply to make the tree look more architectural.

---

# Implementation phases

Status legend:

* **DONE** — implemented and exercised.
* **PARTIAL** — useful implementation exists; hardening remains.
* **NEXT** — immediate work.
* **PLANNED** — intended future work.
* **OPTIONAL** — worthwhile but not required for the core project.
* **DEFERRED** — only implement if real behavior justifies it.

---

## Phase 1 - Connection model and lifecycle — DONE / PARTIAL

### Connection event representation — DONE

Useful connection information is grouped into `struct connection_event`.

Current information includes:

* connection number;
* listening port;
* remote source port;
* remote address;
* persistent seen count;
* local timestamp;
* UTC timestamp for persistence;
* bounded payload;
* payload length.

Long-running counters use `uint64_t`.

IPv6-sized address buffers use `INET6_ADDRSTRLEN`.

### Signal handling — DONE

Handled:

* `SIGINT`;
* `SIGTERM`;
* `SIGPIPE`.

`SIGINT` and `SIGTERM` request graceful shutdown.

`SIGPIPE` is ignored so a disappearing client cannot kill tcpnoise during banner transmission.

### Cleanup — DONE / PARTIAL

Normal and major fatal paths clean up:

* accepted sockets;
* listener sockets;
* text log handles;
* banner memory;
* SQLite connection.

Remaining hardening:

* continue auditing cleanup return values;
* keep cleanup best-effort so one cleanup failure does not prevent other cleanup;
* avoid blindly retrying `close()` where platform semantics make that unsafe.

---

## Phase 2 - Multiple listening ports — DONE

Supports:

```sh
./tcpnoise 23 80 2222 2323
```

Implemented:

* at least one port required;
* malformed-value rejection;
* range validation;
* duplicate suppression;
* maximum configured port count;
* per-port connection counters;
* shared IPv4/IPv6 per-port state.

Future argument hardening:

* set `errno = 0` before `strtol()`;
* explicitly handle `ERANGE`.

This is correctness cleanup rather than a blocker for deployment.

---

## Phase 3 - IPv6 — DONE / PARTIAL

Implemented:

* `sockaddr_storage`;
* numeric remote address lookup;
* numeric source-port lookup;
* explicit IPv4 listener;
* explicit IPv6 listener;
* `IPV6_V6ONLY`;
* bracketed IPv6 endpoint formatting.

Remaining policy decision:

### One-family failure

Current startup behavior is strict.

If one requested family cannot bind, startup fails.

Possible future behavior:

```text
IPv4 succeeds + IPv6 fails -> warn and continue
IPv6 succeeds + IPv4 fails -> warn and continue
both fail                  -> fatal
```

Only change this if deployment demonstrates a real portability benefit.

---

## Phase 4 - Connection handling — DONE / DEFERRED

Current path:

```text
accept
  -> optional banner
  -> blocking recv with ~250 ms timeout
  -> SQLite seen update
  -> terminal/log output
  -> close
```

Receive state distinguishes:

* data;
* orderly close;
* timeout;
* interrupted receive;
* receive error.

### Fully event-driven accepted clients — DEFERRED

Do **not** implement a client-state machine merely because it would be more sophisticated.

Only reconsider if real public testing shows the synchronous ~250 ms accepted-client path causes meaningful listener starvation.

Possible future design:

* accepted sockets added to `poll()`;
* per-client state;
* per-client deadlines;
* bounded active-client count.

Threads remain unnecessary unless measurements demonstrate otherwise.

---

## Phase 5 - Optional per-port banners — DONE / PARTIAL

Banner files:

```text
<port>_banner.txt
```

Implemented:

* files loaded once at startup;
* missing file is nonfatal;
* multiple candidates;
* random candidate selection;
* approximately 25% intentional no-banner responses;
* CRLF termination;
* bounded send buffer;
* partial `send()` handling;
* `EINTR` handling;
* zero-byte send handling;
* `SIGPIPE` protection;
* send failure remains connection-local;
* banner status appears in terminal output;
* banner status appears in text logs.

Remaining optional hardening:

* decide whether blank lines are ignored or intentionally represent empty banners;
* document/enforce maximum candidate length more explicitly;
* optionally track exact bytes successfully sent;
* optionally move banner files into a configurable directory.

No protocol state machine should be added merely because a banner resembles a real service.

---

## Phase 6 - Source modularization — DONE

The original large `main.c` has been split into cohesive modules.

### Banner module — DONE

```text
include/banner.h
src/banner.c
```

Owns:

* banner filename construction;
* banner-file loading;
* banner-pool cleanup;
* selection;
* sending.

### Logging module — DONE

```text
include/logging.h
src/logging.c
```

Owns:

* log filename construction;
* payload rendering;
* connection log writing;
* log-handle cleanup.

### Networking module — DONE

```text
include/network.h
src/network.c
```

Owns:

* socket creation;
* bind;
* listen;
* accept;
* receive timeout;
* bounded receive;
* remote endpoint resolution;
* remote endpoint formatting.

### Database module — DONE

```text
include/database.h
src/database.c
```

Owns:

* SQLite open;
* schema initialization;
* persistent seen-IP update;
* SQLite close.

### Cleanup — NEXT

Remove obsolete source files from the old in-memory design:

```text
include/seen.h
src/seen.c
```

They should not remain in the tree once SQLite is the sole seen-count implementation.

---

## Phase 7 - SQLite seen-IP persistence — DONE / PARTIAL

SQLite is now the authoritative seen-IP store.

Database:

```text
tcpnoise.db
```

Current table:

```text
seen_ip
-------
address        TEXT PRIMARY KEY
first_seen_utc TEXT NOT NULL
last_seen_utc  TEXT NOT NULL
seen_count     INTEGER NOT NULL
```

Implemented:

* database opened once;
* database reused for process lifetime;
* table created if missing;
* first address observation inserts count `1`;
* repeated observations increment the count;
* first-seen timestamp remains unchanged;
* last-seen timestamp updates;
* count survives process restarts;
* IPv4 works;
* IPv6 works;
* hostile address strings are parameterized;
* timestamps are parameterized;
* `RETURNING seen_count` provides the authoritative value;
* database timestamps use UTC;
* console/text logs remain local time.

Current UPSERT behavior conceptually:

```sql
INSERT ...
ON CONFLICT(address) DO UPDATE SET
    last_seen_utc = excluded.last_seen_utc,
    seen_count = seen_count + 1
RETURNING seen_count;
```

### Database failure policy — PARTIAL

Current startup behavior:

```text
database cannot open      -> fatal startup failure
schema initialization fails -> fatal startup failure
```

Connection-time persistence failure currently reports an error and the connection can continue with a fallback seen value.

This policy is acceptable for now.

Later decide whether a runtime database failure should:

* keep warning for each failed event;
* disable persistence after failure;
* attempt recovery;
* stop tcpnoise.

Avoid complicated recovery behavior until a real failure mode justifies it.

### SQLite hardening still available

Later cleanup:

* check `sqlite3_finalize()` results consistently;
* consider stepping a `RETURNING` statement through `SQLITE_DONE` after reading its row;
* define schema versioning if schema migration becomes necessary;
* optionally make database path configurable;
* evaluate WAL only if measurements or concurrent readers justify it.

Do not enable SQLite knobs merely because they exist.

---

## Phase 8 - Linux/VPS deployment and portability — NEXT

This is the immediate phase.

tcpnoise has been exercised on FreeBSD.

Now prove the current modular + SQLite implementation on Ubuntu/Linux.

### Build dependencies

FreeBSD:

```sh
pkg install sqlite3 pkgconf
```

Ubuntu/Debian:

```sh
sudo apt install build-essential pkg-config libsqlite3-dev
```

### First VPS test

Start with a nonprivileged port:

```sh
./tcpnoise 2222
```

Verify:

* IPv4 listener starts;
* IPv6 listener starts;
* external IPv4 connection appears;
* external IPv6 connection appears;
* payload capture works;
* banner behavior works;
* text logging works;
* SQLite row is created;
* repeated IP increments;
* process restart preserves count;
* `Ctrl+C` shuts down cleanly.

Query:

```sh
sqlite3 tcpnoise.db \
    'SELECT * FROM seen_ip ORDER BY seen_count DESC;'
```

### Firewall

Public watched ports must be explicitly allowed.

IPv4 and IPv6 firewall behavior should both be tested.

### Privileged ports

Ports below 1024 should not justify running the full process as root indefinitely.

On Linux, prefer mechanisms such as:

```text
CAP_NET_BIND_SERVICE
```

or service-manager capability configuration.

Deployment privilege handling should remain separate from networking logic.

---

## Phase 9 - Text logging hardening — PLANNED

Current logging is useful and should remain available even with SQLite.

Implemented:

* one log per configured port;
* one open `FILE *` per port;
* IPv4/IPv6 listeners share the file;
* escaped payload preview;
* banner state;
* receive state;
* explicit flush after events;
* cleanup at shutdown.

Remaining work:

### Write-result handling

Audit relevant:

* `fprintf()`;
* `fputc()`;
* `fflush()`;
* `fclose()`.

A broken text log should not automatically stop tcpnoise unless there is a strong reason.

Possible policy:

```text
first persistent log failure
    -> report
    -> disable that log
    -> continue observing traffic
```

Only implement if useful.

### Configurable paths

Possible future layout:

```text
logs/23.log
logs/80.log
tcpnoise.db
```

Configuration can wait until deployment makes current-working-directory storage inconvenient.

### Rotation

Do not implement log rotation inside tcpnoise.

Use:

* `logrotate`;
* service-manager tooling;
* operating-system facilities.

---

## Phase 10 - Optional SQLite connection-event history — OPTIONAL

Seen-IP persistence solves a clear current problem.

A full connection-event table should only be added if there is an actual query/use case.

Possible table:

```text
connection_event
----------------
id
occurred_utc
listen_port
address_family
remote_address
remote_port
seen_count
receive_result
bytes_received
banner_result
payload_preview
```

Potential reasons:

* query events by IP;
* query events by port;
* historical statistics;
* scanner classification history;
* future TUI backing store;
* avoid parsing text logs.

If implemented:

* reuse the existing SQLite connection;
* keep payload bounded;
* parameterize all hostile values;
* decide whether to store raw bytes or an encoded preview;
* use transactions sensibly;
* keep text logging independent.

Do not dual-write merely because two storage systems exist.

---

## Phase 11 - Mission Control telemetry — PLANNED

Mission Control integration remains worthwhile but must be best effort.

Rule:

> Mission Control failure must never stop tcpnoise from observing connections.

### Configuration

Possible environment variables:

```text
TCPNOISE_MC_ENABLED
TCPNOISE_MC_URL
TCPNOISE_MC_API_KEY
```

Never commit the API key.

Never write the key to:

* logs;
* SQLite;
* banner files;
* source;
* telemetry error messages.

### HTTP dependency

Use `libcurl`.

Do not implement HTTP/TLS manually.

Add the dependency deliberately and keep FreeBSD/Linux build support.

### Event

Possible event type:

```text
tcpnoise.connection.accepted
```

Possible payload:

```json
{
  "connectionNumber": 123,
  "listenPort": 23,
  "remoteAddress": "164.92.115.22",
  "remotePort": 49123,
  "addressFamily": "ipv4",
  "seenCount": 37,
  "bytesReceived": 0,
  "receiveResult": "timeout",
  "bannerResult": "sent"
}
```

Payload previews, if included, must remain bounded and JSON-safe.

Do not automatically publish full banner contents.

### Delivery policy

```text
publish succeeds -> continue
publish fails     -> warn if useful, continue
publish times out -> continue
```

Do not synchronously retry in the accepted-client path.

If telemetry latency becomes measurable, consider a small bounded queue later.

---

## Phase 12 - Scanner/probe classification — PLANNED

This is likely one of the more interesting next features after deployment.

Classify only the bounded bytes tcpnoise already captured.

Possible classifications:

* HTTP request;
* TLS ClientHello;
* SSH banner;
* Telnet negotiation;
* common login/bot probe;
* empty/banner-waiting connection;
* unknown text;
* unknown binary data.

Rules:

* never assume payload is NUL-terminated;
* use explicit lengths;
* classification must never make connection handling unsafe;
* classify conservatively;
* unknown is a perfectly valid result.

Real-world background traffic is the point of this feature.

A TLS ClientHello arriving on TCP/23 should be classified as TLS-like traffic, not interpreted as proof that port 23 is actually running TLS.

---

## Phase 13 - Statistics and terminal history — PLANNED

Possible future statistics:

* total connections;
* per-port connections;
* receive-result counts;
* banner sent/none/failed counts;
* classification totals;
* recent IPs;
* top IPs;
* IPv4/IPv6 totals.

Possible terminal features:

* bounded scrollback;
* per-port views;
* per-port tabs;
* recent events;
* top/recent addresses.

Keep the first implementation simple.

Do not add ncurses or another UI dependency until ordinary terminal output becomes an actual limitation.

If SQLite connection-event history exists by then, decide whether the TUI:

* queries SQLite;
* keeps a bounded in-memory event buffer;
* uses both.

---

# Remaining correctness/hardening backlog

These are worthwhile but should not prevent the current program from being deployed and observed.

## Argument parsing

* set `errno = 0` before `strtol()`;
* detect `ERANGE`.

## Timestamp handling

Current behavior intentionally separates:

```text
terminal/log -> local time
SQLite       -> UTC
```

Later:

* derive local and UTC forms from the same `time_t`;
* consider `localtime_r()` / `gmtime_r()` where portable and useful.

## Formatting

Continue checking:

* `snprintf()`;
* `fprintf()`;
* `printf()`;
* `fputc()`;
* flush/close results.

Do not make harmless display failures unnecessarily fatal.

## Networking

* continue checking helper return values;
* decide one-family bind policy;
* revisit synchronous accepted clients only if real load demonstrates a problem.

## Database

* harden `sqlite3_finalize()` handling;
* define migration/version behavior if schema evolves;
* decide runtime database-failure policy;
* optionally configure DB path.

## Banners

* define blank-line behavior;
* clearly document maximum candidate length;
* optionally record exact bytes sent.

---

# Deployment notes

## SQLite database

The database is currently stored in the process working directory:

```text
tcpnoise.db
```

For a service deployment, choose a persistent writable working directory.

Back up the database like any other useful state file.

SQLite is appropriate for the current single-process writer design.

Do not introduce an external database server.

## Logs

Text logs are also currently relative to the working directory.

Use OS log-rotation facilities for long-running deployment.

## Banner files

Banner files are configuration.

They are loaded once at startup.

Restart tcpnoise after changing banner files if reloading is desired.

## Public exposure

tcpnoise is intentionally exposed to unsolicited network traffic.

Keep the program itself unprivileged wherever possible.

Do not interpret banner behavior as a security boundary.

---

# Review checklist

## Networking

* [x] Multiple ports.
* [x] Duplicate port suppression.
* [x] `SO_REUSEADDR`.
* [x] IPv4 listeners.
* [x] IPv6 listeners.
* [x] `IPV6_V6ONLY`.
* [x] Remote source port.
* [x] Accepted sockets closed.
* [x] Listener sockets cleaned up.
* [x] FreeBSD testing after major refactor.
* [ ] Linux testing after modularization + SQLite.
* [ ] Public IPv4 VPS test.
* [ ] Public IPv6 VPS test.
* [ ] Decide one-family bind policy.

## Signals/lifecycle

* [x] `SIGINT`.
* [x] `SIGTERM`.
* [x] `SIGPIPE`.
* [x] Banner cleanup.
* [x] Text log cleanup.
* [x] SQLite cleanup.
* [x] Major fatal paths close SQLite.

## Payload

* [x] Bounded.
* [x] Explicit byte length.
* [x] Not treated as a C string.
* [x] Escaped output.
* [x] Timeout/close/error states.
* [ ] Continue terminal-safety review as classification grows.

## Seen IP / database

* [x] IPv4.
* [x] IPv6.
* [x] Persistent count.
* [x] First-seen timestamp.
* [x] Last-seen timestamp.
* [x] UTC database timestamps.
* [x] One long-lived SQLite connection.
* [x] Parameterized network values.
* [x] SQLite authoritative for seen counts.
* [x] Fixed 1000-IP runtime limit removed.
* [ ] Remove obsolete `seen.c` / `seen.h`.
* [ ] Harden finalize-result handling.
* [ ] Define schema migration/version strategy when needed.
* [ ] Define long-running database-failure policy.

## Banners

* [x] Missing files nonfatal.
* [x] Loaded once.
* [x] Multiple candidates.
* [x] Intentional no-banner path.
* [x] Partial sends.
* [x] `EINTR`.
* [x] `SIGPIPE` protection.
* [x] Terminal state.
* [x] Log state.
* [ ] Blank-line policy.
* [ ] Candidate-length documentation.
* [ ] Optional exact bytes-sent tracking.

## Logging

* [x] Per-port files.
* [x] Persistent handles.
* [x] IPv4/IPv6 sharing.
* [x] Immediate flush.
* [x] Local timestamps.
* [x] Cleanup.
* [ ] Full stdio error audit.
* [ ] Optional configurable directory.
* [ ] Optional SQLite event history.

## Portability

* [x] Built/exercised on FreeBSD.
* [x] SQLite build uses `pkg-config`.
* [ ] Build current version on Ubuntu.
* [ ] Exercise current version on Ubuntu.
* [ ] Verify warning-clean Linux build.
* [ ] Verify IPv6 on deployed Linux host.

## Mission Control

* [ ] Configuration.
* [ ] libcurl integration.
* [ ] bounded valid JSON.
* [ ] secret handling.
* [ ] best-effort failure policy.
* [ ] latency test.

---

# Definition of core done

The core tcpnoise project can be considered solid when one binary can run like:

```sh
./tcpnoise 23 80 2222 2323
```

and reliably:

* binds the requested ports;
* supports IPv4 and IPv6;
* watches listeners concurrently;
* accepts and closes connections correctly;
* reports listening port;
* reports remote address and source port;
* maintains per-port connection totals;
* persistently tracks seen-IP counts;
* records first/last seen history;
* safely previews bounded payload data;
* optionally sends bounded banners;
* writes useful per-port text logs;
* shuts down cleanly;
* builds cleanly with project warning flags on FreeBSD and Linux.

At the current point, most of that functionality exists.

The major remaining requirement before calling the **core** done is:

> **exercise the current modular + SQLite implementation on Linux/VPS as well as FreeBSD.**

Mission Control, scanner classification, event-history SQLite, statistics, scrollback, and per-port terminal views are extensions.

They should make tcpnoise more interesting.

They should not prevent an already-useful tcpnoise from ever being considered finished.
