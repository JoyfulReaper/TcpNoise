# tcpnoise plan

`tcpnoise` is a small POSIX C program for observing unsolicited TCP connection noise on public ports.

The project should stay understandable and portable between FreeBSD and Linux. The goal is not to turn it into a packet analyzer, IDS, honeypot framework, or networking framework.

## Working style

Implementation will continue in small slices:

1. Kyle writes each slice.
2. Review the change for correctness, portability, ownership, cleanup, and C/POSIX issues.
3. Fix problems before moving on.
4. Prefer correct error handling over quick-and-dirty shortcuts.
5. Do not replace the project with a complete implementation unless explicitly requested.

Keep each slice small enough to understand and test independently.

---

# Current status

As of 2026-09-05, the original single-port experiment has grown into a useful multi-port network-noise observer.

Working now:

- multiple TCP ports in one process;
- explicit IPv4 and IPv6 listeners;
- `poll()` across listening sockets;
- remote IPv4/IPv6 address and source-port reporting;
- per-port connection counters;
- in-memory seen-IP counts;
- bounded payload capture with distinct receive states;
- graceful `SIGINT` / `SIGTERM` shutdown;
- `SIGPIPE` protection;
- optional per-port banner files loaded once at startup;
- multiple candidate banner lines per port;
- random banner selection with an intentional chance of sending no banner;
- safe banner sending with partial-write and `EINTR` handling;
- console reporting for banner sent / none / failed;
- per-port text logs;
- banner result and receive result written to logs;
- one persistent open log file per configured port rather than reopening it for every connection;
- explicit flushing after each logged event;
- cleanup of listeners, banner memory, and log handles across normal and fatal paths.

The current synchronous accepted-client receive timeout is 250 ms. Real public traffic has not yet demonstrated enough listener starvation to justify a fully event-driven accepted-client state machine.

The immediate next goal is to split the now-large `main.c` into a few cohesive modules before adding more features.

---

# Goals

- Listen on one or more TCP ports from a single process.
- Support both IPv4 and IPv6 explicitly.
- Count total accepted connections per configured port.
- Track how many times each remote IP has been seen.
- Persist useful seen-IP history across restarts with SQLite.
- Safely display a bounded payload preview.
- Keep useful per-port text logs.
- Optionally support SQLite-backed connection/event history if it proves useful.
- Gracefully stop on signals.
- Optionally send configured per-port banners after accepting a connection.
- Optionally publish best-effort events to Mission Control.
- Remain usable on FreeBSD and Linux.
- Keep dependencies deliberate and minimal.
- Remain small enough to understand as a learning project.

Example usage:

```sh
./tcpnoise 23 80 2222 2323
```

Optional banner files use the convention:

```text
23_banner.txt
80_banner.txt
2222_banner.txt
2323_banner.txt
```

Each non-empty line is a candidate banner. A configured connection may intentionally receive no banner.

A missing banner file is normal and must not prevent the listener from starting.

---

# Non-goals

For now, tcpnoise will not:

- capture raw SYN packets;
- detect scans that never complete a TCP handshake;
- implement a full Telnet, HTTP, SSH, TLS, FTP, or other application protocol server;
- become a general-purpose honeypot framework;
- attempt to exploit, retaliate against, or aggressively interact with scanners;
- implement its own TLS stack;
- implement its own HTTP client;
- implement its own database engine;
- implement log rotation internally;
- become multithreaded merely because threads exist.

An accepted TCP connection is the unit tcpnoise observes.

Banners are intentionally shallow. Sending text that resembles a service banner is in scope. Implementing the corresponding protocol state machine is not.

---

# Target architecture

Conceptually:

```text
command-line ports
       |
       v
per-port state
       |
       +--> port 23
       |      banner pool
       |      text log
       |      IPv4 listener
       |      IPv6 listener
       |
       +--> port 80
       |      banner pool
       |      text log
       |      IPv4 listener
       |      IPv6 listener
       |
       +--> ...
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
brief payload receive
       |
       v
build connection event
       |
       +--> terminal output
       +--> per-port text log
       +--> seen-IP update
       +--> optional SQLite persistence
       +--> optional Mission Control event
```

IPv4 and IPv6 use explicit listeners rather than relying on IPv4-mapped IPv6 behavior.

IPv6 listener sockets use `IPV6_V6ONLY` so behavior remains predictable across FreeBSD and Linux.

Banner files and text log files are opened/loaded once during startup rather than once per accepted connection.

SQLite, when added, should be opened once and reused for the life of the process.

---

# Implementation phases

Status legend:

- **DONE** — implemented and exercised.
- **PARTIAL** — useful implementation exists, but planned hardening remains.
- **NEXT** — immediate work.
- **PLANNED** — intended future work.
- **DEFERRED** — only do it if real behavior justifies it.

---

## Phase 1 - Connection model and cleanup — DONE / PARTIAL

### Slice 1 - Introduce `struct connection_event` — DONE

Connection information is represented as a struct rather than a loose pile of variables.

Current useful fields include:

- connection number;
- listening port;
- remote source port;
- remote address;
- seen count;
- timestamp;
- payload;
- payload length.

Long-running connection counters use `uint64_t`.

IPv6-sized address buffers use `INET6_ADDRSTRLEN`.

### Slice 2 - Harden formatting and helper errors — PARTIAL

Already handled in important paths:

- `snprintf()` failure/truncation;
- timestamp failure fallback;
- address-resolution failure;
- socket/setup failures;
- banner-send errors;
- log open/flush/close errors.

Remaining cleanup:

- consistently check all `fprintf()` / `fputc()` results where practical;
- continue reviewing helper ownership and cleanup behavior;
- consider `localtime_r()` where portability and simplicity make sense.

Do not turn every display failure into a fatal listener failure.

### Slice 3 - Socket setup cleanup — DONE

Implemented:

- `SO_REUSEADDR`;
- reasonable listen backlog;
- listener helpers;
- cleanup helpers.

### Slice 4 - Signal cleanup — DONE

Handled:

- `SIGINT`;
- `SIGTERM`;
- `SIGPIPE` ignored for connection-local send failure behavior.

Normal shutdown closes listeners, closes logs, frees banners, and prints final per-port counts.

---

## Phase 2 - Multiple listening ports — DONE

### Slice 5 - Parse multiple port arguments — DONE

Supports:

```sh
./tcpnoise 23 80 2222 2323
```

Includes:

- at least one port;
- range validation;
- malformed-value rejection;
- duplicate suppression;
- maximum configured port count.

### Slice 6 - Listener representation — DONE

Each listener tracks:

- socket descriptor;
- listening port;
- address family;
- configured-port index;
- banner pool reference;
- shared per-port log reference.

### Slice 7 - Watch multiple listeners with `poll()` — DONE

One process watches all IPv4/IPv6 listening sockets.

---

## Phase 3 - IPv6 — DONE

### Slice 8 - Address-family-independent peer storage — DONE

Accepted peers use `struct sockaddr_storage`.

### Slice 9 - Numeric remote address and source port — DONE

`getnameinfo()` is used with numeric flags.

No reverse DNS lookup is performed in the connection path.

### Slice 10 - Explicit IPv4 and IPv6 listeners — DONE

Every configured port currently creates explicit:

- `AF_INET` listener;
- `AF_INET6` listener with `IPV6_V6ONLY`.

Future hardening:

- decide whether one-family failure should merely warn when the other family successfully binds;
- retain predictable behavior on both FreeBSD and Linux.

---

## Phase 4 - Connection handling — DONE / DEFERRED

### Slice 11 - Extract accepted-client handling from `main()` — DONE

Accepted-client work lives in `handle_connection()`.

### Slice 12 - Preserve receive state — DONE

Receive results distinguish:

- data;
- orderly peer close;
- timeout;
- interrupted receive;
- receive error.

### Slice 13 - Prevent quiet clients from stalling listeners — DEFERRED

Current behavior:

```text
accept
  -> optional banner
  -> blocking recv with ~250 ms timeout
  -> record event
  -> close connection
```

Public testing has not yet shown enough harm to justify a more complex accepted-client event loop.

Only if measurements justify it:

- add accepted client sockets to `poll()`;
- track per-client deadlines/state;
- avoid threads unless there is a demonstrated reason.

---

## Phase 5 - Optional per-port banners — DONE / PARTIAL

Banner files use:

```text
<port>_banner.txt
```

Each line is currently treated as one candidate textual banner.

### Slice 14 - Load banner pools once at startup — DONE

Implemented:

- one banner pool per configured port;
- missing file means no banners;
- line-based candidates;
- memory cleanup;
- both IPv4 and IPv6 listeners reference the same per-port pool.

Current implementation also intentionally chooses no banner for approximately 25% of connections.

Future hardening:

- decide whether blank lines should be ignored or intentionally mean an empty banner;
- impose/document a clear maximum candidate length;
- make banner directory configurable only if useful.

### Slice 15 - Send banner safely after accept — DONE

Implemented:

- CRLF appended;
- partial `send()` handling;
- `EINTR` retry;
- zero-byte send handling;
- oversized encoded-banner rejection;
- `SIGPIPE` protection;
- banner-send failure remains connection-local.

### Slice 16 - Record banner result — PARTIAL

Implemented states:

- no banner selected;
- banner sent successfully;
- banner selected but send failed.

These are shown in terminal output and text logs.

Still optional:

- exact banner bytes successfully sent;
- explicit partial-send result;
- richer banner metadata in `connection_event`.

Do this before Mission Control if telemetry needs those fields.

---

## Phase 6 - Source modularization — NEXT

`main.c` has grown large enough that cohesive modules now improve clarity.

Do not split everything at once.

### Slice 17 - Extract banner module — NEXT

Create:

```text
include/banner.h
src/banner.c
```

Move banner-specific code such as:

- banner filename construction;
- banner-file loading;
- banner-pool cleanup;
- banner selection;
- banner sending.

Keep ownership rules explicit.

Update the Makefile to compile/link the new source file.

Build and test before moving another subsystem.

### Slice 18 - Extract logging module — PLANNED

Create:

```text
include/logging.h
src/logging.c
```

Move:

- log filename construction;
- log opening/closing helpers;
- connection log formatting/writing;
- log flushing.

Do not let the logging module take ownership of resources it does not own.

### Slice 19 - Extract seen-IP module — PLANNED

Create:

```text
include/seen.h
src/seen.c
```

Move:

- lookup;
- increment/update;
- bounded-table policy;
- later SQLite persistence interface.

### Slice 20 - Extract networking helpers — PLANNED

Only after the previous boundaries feel natural.

Possible files:

```text
include/network.h
src/network.c
```

Candidates:

- socket creation;
- bind/listen helpers;
- accept helper;
- remote endpoint resolution;
- receive timeout / payload receive.

Keep `main.c` responsible for orchestration and the main event loop.

### Possible later shared types

If cross-module structs become awkward, introduce a small shared header such as:

```text
include/tcpnoise.h
```

or:

```text
include/types.h
```

Do not create a giant dumping-ground header preemptively.

---

## Phase 7 - Seen-IP tracking and SQLite persistence — PARTIAL / PLANNED

### Slice 21 - IPv6-capable in-memory seen table — DONE

The current table stores IPv4 and IPv6 textual addresses.

A bounded linear table remains acceptable at the current scale.

### Slice 22 - Define full-table behavior — PLANNED

The current fixed table eventually fills.

Choose a predictable policy that does not repeatedly spam errors.

Possible policies:

- stop admitting new addresses but continue updating known ones;
- replace oldest entries;
- maintain a bounded rolling cache.

Do not let table exhaustion affect connection acceptance.

### Slice 23 - Persist seen-IP history in SQLite — PLANNED

This is now a real goal because useful scanners can reconnect hundreds of times and the in-memory count resets every process restart.

Use SQLite deliberately rather than inventing a storage format.

Possible database:

```text
tcpnoise.db
```

Possible table:

```text
seen_ip
-------
address        TEXT PRIMARY KEY
first_seen_utc TEXT NOT NULL
last_seen_utc  TEXT NOT NULL
seen_count     INTEGER NOT NULL
```

Exact schema can change when implemented.

Desired behavior:

- database opened once at startup;
- schema created/migrated deliberately;
- update existing row when an address is seen;
- insert new row on first observation;
- preserve counts across restarts;
- SQLite failure should be reported clearly;
- decide whether database failure is fatal at startup or disables persistence while tcpnoise continues;
- avoid opening/closing SQLite on every event;
- use prepared statements rather than rebuilding SQL strings for every connection;
- never concatenate hostile network strings into SQL.

Decide whether the in-memory table remains a fast session cache layered over SQLite or whether SQLite becomes authoritative for seen counts.

Prefer the simplest design that keeps the hot path understandable.

---

## Phase 8 - Logging and optional SQLite event history — DONE / PLANNED

### Slice 24 - Persistent per-port text logs — DONE

Current text logs remain useful:

```text
23.log
80.log
2222.log
2323.log
```

Implemented:

- one `FILE *` opened per configured port;
- IPv4 and IPv6 listeners share that per-port file;
- log handles stay open for the process lifetime;
- each event is flushed;
- handles are closed exactly once during cleanup;
- payloads are escaped before logging;
- banner state is included.

Text logging should remain available even if SQLite is later added.

### Slice 25 - Harden log writes — PLANNED

Review all writes for return-value handling.

A failed log write should not crash the listener unless there is a strong reason.

Decide whether a persistently broken log should be disabled after repeated failures rather than producing endless error output.

### Slice 26 - Configurable log path — PLANNED

Optionally support a directory such as:

```text
./logs/23.log
```

Do not implement rotation internally.

Document operating-system `logrotate` or equivalent configuration instead.

### Slice 27 - Evaluate SQLite connection/event logging — OPTIONAL

SQLite may also be useful for querying historical connection events, but do not automatically replace the simple text logs.

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

Before implementing, decide what problem SQLite event logging solves:

- querying by IP;
- querying by port;
- long-term statistics;
- feeding a future TUI;
- avoiding text-log parsing.

If implemented:

- reuse one long-lived SQLite connection;
- use prepared statements;
- use transactions sensibly;
- keep payload storage bounded;
- decide whether raw payload bytes or only escaped/bounded previews belong in the database;
- text logs may remain enabled alongside SQLite or become independently configurable.

Do not dual-write merely because it is possible.

---

## Phase 9 - Mission Control telemetry — PLANNED

Mission Control integration is optional and best effort.

Mission Control failure must never stop tcpnoise from accepting connections.

### Slice 28 - Define telemetry configuration

Possible environment variables:

```text
TCPNOISE_MC_ENABLED
TCPNOISE_MC_URL
TCPNOISE_MC_API_KEY
```

Never commit the API key.

### Slice 29 - Add HTTP dependency deliberately

Use `libcurl`.

Do not hand-roll HTTP or TLS.

Use `pkg-config` / platform linker flags in a FreeBSD/Linux-friendly way.

### Slice 30 - Define the connection event

Suggested type:

```text
tcpnoise.connection.accepted
```

Suggested metadata:

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
  "bannerResult": "sent",
  "payloadPreview": null
}
```

Do not automatically publish complete banner contents.

Payload previews must remain bounded and safely encoded.

### Slice 31 - Build valid Mission Control envelopes

Publish to the configured Mission Control API with:

- unique event ID;
- event type;
- schema version;
- UTC occurrence timestamp;
- JSON object payload.

Do not concatenate hostile payload bytes into JSON without escaping.

### Slice 32 - Best-effort delivery

Policy:

```text
publish succeeds -> continue
publish fails     -> optionally warn, continue
publish times out -> continue
```

Do not synchronously retry in the accepted-client path.

If telemetry latency becomes noticeable, consider a small bounded queue later.

---

## Phase 10 - Optional scanner classification — PLANNED

Classify only the bounded bytes tcpnoise already captured.

Possible classifications:

- HTTP request;
- TLS ClientHello;
- SSH banner;
- Telnet negotiation;
- common bot/login probe;
- empty/banner-waiting connection;
- unknown binary payload.

Real public traffic has already demonstrated useful examples such as TLS ClientHello payloads arriving on unexpected ports.

Never assume payload data is NUL-terminated.

Do not confuse tcpnoise's outbound banner with an inbound scanner payload.

---

## Phase 11 - Terminal history, per-port views, and statistics — PLANNED

Possible future TUI features:

- scrollback buffer for recent events;
- per-port tabs/views;
- current connection totals;
- per-port totals;
- receive-result counts;
- probe classifications;
- banner sent/none/failed counts;
- top/recent IPs.

Keep the first implementation simple.

Do not add ncurses or another UI dependency until the current plain-terminal output becomes a real limitation.

If SQLite connection history is implemented first, evaluate whether the TUI should query it or maintain a bounded in-memory event buffer.

---

# Suggested source layout

Near-term target:

```text
07-tcpnoise/
├── Makefile
├── README.md
├── plan.md
├── banners.txt
├── include/
│   ├── banner.h
│   ├── logging.h
│   ├── network.h
│   └── seen.h
└── src/
    ├── main.c
    ├── banner.c
    ├── logging.c
    ├── network.c
    └── seen.c
```

Later, if justified:

```text
include/
    telemetry.h
    database.h
src/
    telemetry.c
    database.c
```

A dedicated database module may be useful if both seen-IP persistence and optional event logging share one SQLite connection.

This is a target, not a rule.

Do not create a `.c`/`.h` pair for every tiny helper just to make the tree look architectural.

---

# Deployment notes

## Privileged ports

Ports below 1024 normally require additional privilege.

Avoid running the complete process as root long-term merely to bind ports such as 23 or 80.

Possible approaches:

- Linux `CAP_NET_BIND_SERVICE`;
- service-manager capabilities;
- bind before dropping privileges.

Keep deployment privilege concerns separate from connection logic.

## Firewall

Every watched port must be allowed by the host firewall if public traffic should reach it.

Verify IPv6 firewall behavior independently.

## Text logs

Use operating-system rotation facilities for long-running deployments.

## SQLite database

When SQLite persistence is added:

- choose a configurable database path;
- document backup expectations;
- use SQLite locking/journal behavior appropriate for a single-process writer;
- do not require an external database server.

WAL mode can be evaluated if there is a demonstrated benefit; do not enable knobs merely because they exist.

## Banner files

Treat banner files as configuration.

Load them once at startup.

Keep candidates bounded.

Do not implement fake protocol conversations merely because a banner resembles a real service.

## Mission Control secrets

Never place API keys in:

- source;
- `plan.md`;
- banner files;
- logs;
- SQLite databases;
- Git history.

---

# Review checklist

## Networking

- [x] Multiple ports can be watched simultaneously.
- [x] Duplicate port arguments are handled.
- [x] `SO_REUSEADDR` is enabled.
- [x] Explicit IPv4 and IPv6 listeners exist.
- [x] IPv6 listeners use `IPV6_V6ONLY`.
- [x] Remote source port is captured.
- [x] Accepted client sockets are closed.
- [x] Listener sockets are cleaned up.
- [ ] Verify current behavior again on FreeBSD after major refactors.
- [ ] Verify current behavior again on Linux after major refactors.
- [ ] Decide whether one-family bind failure may continue with the other family.

## Signals and lifecycle

- [x] `SIGINT` shuts down cleanly.
- [x] `SIGTERM` shuts down cleanly.
- [x] `SIGPIPE` cannot unexpectedly terminate tcpnoise.
- [x] Banner memory is freed.
- [x] Persistent text log handles are closed.

## Payload handling

- [x] Receive timeout / close / error are distinguishable.
- [x] Payload bytes are length-bounded.
- [x] Payload bytes are not assumed to be a C string.
- [x] Payload output escapes non-printable bytes.
- [ ] Continue reviewing terminal-safety assumptions as classification grows.

## Formatting and errors

- [x] Major `snprintf()` truncation/failure paths are handled.
- [x] Address-conversion errors are handled.
- [x] Timestamp failures have fallback behavior.
- [ ] Consistently check all relevant stdio write results.
- [ ] Keep cleanup functions best-effort so one failed cleanup does not prevent the rest.

## Seen IP

- [x] IPv6 addresses fit the current table.
- [ ] Define quiet behavior when the in-memory table fills.
- [ ] Persist seen-IP count/history with SQLite.
- [ ] Decide in-memory-vs-SQLite authority/cache model.

## Banners

- [x] Missing banner files are nonfatal.
- [x] Banner files are loaded once.
- [x] Multiple candidate banners are supported.
- [x] A connection may intentionally receive no banner.
- [x] Partial banner writes are handled.
- [x] `EINTR` during banner send is handled.
- [x] Banner-send failure does not stop connection handling.
- [x] Console output records banner state.
- [x] Text logs record banner state.
- [ ] Decide blank-line behavior.
- [ ] Document/enforce candidate length clearly.
- [ ] Optionally track exact bytes sent.

## Logging

- [x] Per-port text logs are retained.
- [x] Text log files stay open for the process lifetime.
- [x] IPv4/IPv6 listeners for one port share one log handle.
- [x] Events are flushed after writing.
- [x] Log handles are cleaned up.
- [ ] Check every relevant log write result.
- [ ] Add optional configurable log directory.
- [ ] Evaluate optional SQLite event logging.

## Database

- [ ] Add SQLite deliberately.
- [ ] Reuse one database connection rather than opening per event.
- [ ] Use prepared statements.
- [ ] Keep hostile network data parameterized.
- [ ] Define schema/version initialization.
- [ ] Define behavior if the database becomes unavailable.

## Mission Control

- [ ] Mission Control failure never stops connection handling.
- [ ] API key is never logged.
- [ ] Telemetry is valid JSON for hostile/binary input.
- [ ] Telemetry stays bounded.
- [ ] Synchronous telemetry does not noticeably stall listeners.

## Portability

- [ ] No accidental Linux-only behavior.
- [ ] No accidental FreeBSD-only behavior.
- [ ] Build remains clean under `-Wall -Wextra -Wpedantic`.
- [ ] Re-test both platforms after source modularization and SQLite integration.

---

# Definition of done

The expanded tcpnoise project is in a solid finished state when one binary can be run like:

```sh
./tcpnoise 23 80 2222 2323
```

and reliably:

- binds requested ports on IPv4 and IPv6 where available;
- watches all listeners concurrently;
- reports the correct listening port;
- reports remote IPv4/IPv6 address and source port;
- tracks per-IP seen counts;
- persists useful seen-IP history across restarts;
- safely previews bounded payload bytes;
- optionally sends bounded per-port banners;
- writes useful per-port text logs;
- shuts down cleanly;
- builds cleanly with project warning flags on FreeBSD and Linux.

Optional but worthwhile additions:

- SQLite connection/event history;
- Mission Control telemetry;
- scanner classification;
- rolling statistics;
- scrollback and per-port terminal views.

Those are extensions, not excuses to prevent the core project from ever being considered finished.
