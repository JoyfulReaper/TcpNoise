# tcpnoise plan

`tcpnoise` is a small POSIX C program for observing unsolicited TCP connection noise on public ports.

The project should stay understandable and portable between FreeBSD and Linux. The goal is not to turn it into a packet analyzer, IDS, honeypot framework, or networking framework.

## Working style

Implementation will be done in small slices:

1. Kyle writes each slice.
2. Review the change for correctness, portability, and C/POSIX issues.
3. Fix problems before moving on.
4. Do not replace the project with a complete implementation unless explicitly requested.

Keep each slice small enough to understand and test independently.

---

## Goals

- Listen on one or more TCP ports from a single process.
- Support both IPv4 and IPv6 explicitly.
- Count total accepted connections.
- Track how many times each remote IP has been seen during the current run.
- Safely display a bounded payload preview.
- Append events to per-port log files.
- Gracefully stop on signals.
- Optionally send a configured per-port banner after accepting a connection.
- Optionally publish best-effort events to Mission Control.
- Remain usable on FreeBSD and Linux.
- Keep dependencies minimal.

Example eventual usage:

```sh
./tcpnoise 23 80 2222 2323
```

The program should be able to watch all requested ports simultaneously.

Optional banner files may be placed beside the program or in a later configurable directory:

```text
23_banner.txt
80_banner.txt
2222_banner.txt
2323_banner.txt
```

If a banner file for a configured port does not exist, tcpnoise should simply accept and observe connections as normal.

---

## Non-goals

For now, tcpnoise will not:

- capture raw SYN packets;
- detect scans that never complete a TCP handshake;
- implement a full Telnet, HTTP, SSH, or other application protocol server;
- emulate real services beyond optionally sending a static configured banner;
- attempt to exploit, retaliate against, or interact aggressively with scanners;
- implement its own TLS stack;
- implement log rotation;
- become a multithreaded framework unless actual load proves that necessary.

An accepted connection is the unit tcpnoise observes.

Banner support is intentionally simple: send bounded static bytes associated with the listening port, then continue with the normal observation path.

---

## Target architecture

Eventually the main loop should conceptually look like:

```text
command-line ports
       |
       v
create listener set
       |
       +--> port 23   IPv4 listener
       |              IPv6 listener
       |              optional banner
       |
       +--> port 80   IPv4 listener
       |              IPv6 listener
       |              optional banner
       |
       +--> port 2222 IPv4 listener
       |              IPv6 listener
       |              optional banner
       |
       +--> ...
       |
       v
     poll()
       |
       v
accept ready connection
       |
       +--> optionally send port banner
       |
       v
build connection_event
       |
       +--> terminal output
       +--> per-port log
       +--> seen-IP tracking
       +--> Mission Control (best effort)
```

IPv4 and IPv6 should use explicit listeners rather than relying on OS-dependent IPv4-mapped IPv6 behavior.

For IPv6 listener sockets, use `IPV6_V6ONLY` so the IPv6 socket only handles IPv6 while the IPv4 socket handles IPv4. This makes behavior predictable across FreeBSD and Linux.

Banner data should be loaded once during startup rather than opening and reading a banner file for every accepted connection.

---

# Implementation slices

## Phase 1 - Connection model and cleanup

### Slice 1 - Introduce `struct connection_event`

Move the information describing one accepted connection into one struct.

Expected fields include:

- connection number;
- listening port;
- remote address string;
- remote source port;
- address family;
- seen count;
- timestamp;
- payload buffer;
- payload length / receive result.

Use `uint64_t` for long-running counters.

Use an address buffer large enough for IPv6 (`INET6_ADDRSTRLEN`).

Keep current behavior unchanged while migrating code to use the struct.

### Slice 2 - Harden formatting and helper errors

Review and correctly handle:

- `inet_ntop()` / address conversion failures;
- `localtime()` or a safer/reentrant alternative;
- `strftime()` failures;
- `snprintf()` failures/truncation;
- file write errors.

Do not let logging or formatting failures unnecessarily kill the listener.

### Slice 3 - Socket setup cleanup

Add:

- `SO_REUSEADDR`;
- a larger reasonable listen backlog;
- helpers for listener creation and cleanup.

Keep listener creation separate from connection handling.

### Slice 4 - Signal cleanup

Handle at least:

- `SIGINT`;
- `SIGTERM`.

Shutdown should close all listener sockets and print the final connection count.

---

## Phase 2 - Multiple listening ports

### Slice 5 - Parse multiple port arguments

Change argument handling from exactly one port to one or more ports.

Example:

```sh
./tcpnoise 23 80 2222 2323
```

Requirements:

- at least one port;
- every port must be in `1..65535`;
- reject malformed values;
- reject or de-duplicate repeated ports;
- impose a sane maximum number of configured ports.

Do not hardcode the watched ports in the binary.

A compile-time default list could be added later if it proves useful, but the command line should remain the primary interface.

### Slice 6 - Listener representation

Introduce a small listener struct containing at least:

- socket file descriptor;
- port;
- address family.

A configured port may produce more than one listening socket.

Example:

```text
port 23
  fd 3 IPv4
  fd 4 IPv6

port 80
  fd 5 IPv4
  fd 6 IPv6
```

Do not add banner ownership to the listener struct yet unless it naturally simplifies the implementation. Banner configuration can be associated with a port in a later slice.

### Slice 7 - Watch multiple listeners with `poll()`

Replace the single blocking `accept()` loop with `poll()` over all listening sockets.

When a listener becomes readable:

1. identify which listening socket fired;
2. know which configured port it belongs to;
3. call `accept()`;
4. create the connection event;
5. continue watching every listener.

This is the core change that allows one process to monitor many ports.

---

## Phase 3 - IPv6

### Slice 8 - Address-family-independent peer storage

Replace IPv4-only peer storage:

- `struct sockaddr_in`

with:

- `struct sockaddr_storage`.

Track the returned address length with `socklen_t`.

### Slice 9 - Numeric remote address and source port

Use an address-family-independent conversion method such as `getnameinfo()` with numeric flags to obtain:

- remote IP address;
- remote source port.

Expected output:

```text
[remote: 164.92.115.22:49123]
```

and:

```text
[remote: 2604:a880:...:beef:49123]
```

Do not perform reverse DNS lookups in the hot path.

### Slice 10 - Create IPv4 and IPv6 listeners

For every configured port, attempt to create:

- an IPv4 listener;
- an IPv6 listener.

Use address-family-independent setup, preferably `getaddrinfo()` with `AI_PASSIVE`.

Make partial availability reasonable:

- if IPv4 succeeds and IPv6 is unavailable, the program may continue with IPv4 after warning;
- if IPv6 succeeds and IPv4 fails, the reverse may also be acceptable;
- if no listener can be created for a requested port, report it clearly.

The startup banner should show what actually bound.

Example:

```text
Listening:
  tcp4 0.0.0.0:23
  tcp6 [::]:23
  tcp4 0.0.0.0:80
  tcp6 [::]:80
```

---

## Phase 4 - Connection handling cleanup

### Slice 11 - Extract connection handling from `main()`

Move the accepted-client work into a focused function.

`main()` should eventually mostly:

1. parse configuration;
2. install signals;
3. create listeners;
4. call the event loop;
5. clean up.

Connection handling should own:

- receive timeout;
- payload receive;
- connection event construction;
- seen counter update;
- output/logging;
- optional banner send;
- telemetry trigger.

### Slice 12 - Improve receive-state reporting

Currently timeout, orderly close, and some errors can all appear as no payload.

Track a small receive state such as:

- `payload`;
- `closed`;
- `timeout`;
- `error`.

Keep the human terminal output simple, but preserve the distinction for logging and Mission Control.

### Slice 13 - Prevent one quiet client from stalling everything

The current per-connection `recv()` may block for up to 250 ms.

Start simple and measure.

If public traffic demonstrates that this is a bottleneck, extend `poll()` to also track accepted client sockets rather than blocking during payload collection.

Do not add threads merely because they exist.

---

## Phase 5 - Optional per-port banners

Banner support should remain optional and simple.

The initial convention is:

```text
<port>_banner.txt
```

Examples:

```text
23_banner.txt
80_banner.txt
2222_banner.txt
2323_banner.txt
```

A missing banner file is not an error.

### Slice 14 - Discover and load banner files

For each configured port, check once during startup whether a matching banner file exists.

Requirements:

- do not reopen the banner file for every connection;
- store banner bytes and length in memory;
- impose a reasonable maximum banner size;
- treat banner contents as bytes, not necessarily a C string;
- a missing file means no banner for that port;
- a read error on an existing banner file should warn clearly but should not prevent unrelated ports from starting.

Startup output may optionally indicate banner status:

```text
Listening:
  tcp4 0.0.0.0:2222  banner=2222_banner.txt
  tcp6 [::]:2222     banner=2222_banner.txt
  tcp4 0.0.0.0:2323  banner=none
```

A configurable banner directory can be added later if useful.

### Slice 15 - Send banner after accept

If the accepted connection belongs to a port with configured banner data, attempt to send it immediately after `accept()`.

Requirements:

- handle partial writes correctly;
- handle interrupted writes;
- do not assume one `send()` transmits the entire banner;
- avoid process termination from `SIGPIPE`;
- a failed banner send must not kill tcpnoise;
- after the banner attempt, continue with the normal receive/logging path.

The initial behavior should be static and predictable:

```text
accept
  -> optional banner send
  -> wait briefly for payload
  -> record event
```

Do not implement protocol conversations, prompts, login flows, or state machines.

### Slice 16 - Record banner-send result

Extend the event/logging model enough to preserve useful banner information, such as:

- no banner configured;
- banner sent;
- partial send;
- send failed;
- number of banner bytes successfully sent.

Keep terminal output compact.

Mission Control can later include these fields when telemetry is added.

---

## Phase 6 - Seen-IP tracking

### Slice 17 - IPv6-capable seen table

Update seen-IP storage for IPv6 addresses.

Initially the existing bounded array and linear search are acceptable because the project is small.

### Slice 18 - Define behavior when the table fills

The current fixed seen table eventually fills permanently.

Choose a predictable policy, for example:

- replace the oldest entry;
- maintain a fixed rolling table;
- stop tracking new addresses without repeated error spam.

Do not let table exhaustion break connection handling.

If traffic later justifies it, replace the linear table with a small hash table. Do not preemptively build one.

---

## Phase 7 - Logging

### Slice 19 - Keep per-port logs

Retain the current useful behavior:

```text
23.log
80.log
2222.log
2323.log
```

Each accepted event is written to the log associated with the listening port.

Continue escaping payload bytes before writing them.

Include banner-send state when useful without making logs noisy.

### Slice 20 - Make log path configurable

Optionally support a log directory, such as:

```text
./logs/23.log
```

Do not implement rotation in tcpnoise.

Document an example `logrotate` configuration instead.

A later configuration option may also allow banner files to live in a dedicated directory.

---

## Phase 8 - Mission Control telemetry

Mission Control integration is optional and best effort.

A Mission Control failure must never stop tcpnoise from accepting connections.

### Slice 21 - Define telemetry configuration

Read configuration from environment variables rather than source code.

Possible variables:

```text
TCPNOISE_MC_ENABLED
TCPNOISE_MC_URL
TCPNOISE_MC_API_KEY
```

The API key must never be committed to Git.

### Slice 22 - Add HTTP dependency deliberately

Use `libcurl` for HTTP/HTTPS.

Do not implement an HTTP or TLS client manually.

Update the Makefile using the platform's `pkg-config` / curl linker flags in a way that works on FreeBSD and Linux.

Telemetry support should ideally be compile-time optional if libcurl is not desired.

### Slice 23 - Define the event

Suggested Mission Control event type:

```text
tcpnoise.connection.accepted
```

Schema version starts at `1`.

Suggested payload:

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
  "bannerConfigured": true,
  "bannerBytesSent": 18,
  "bannerResult": "sent",
  "payloadPreview": null
}
```

For IPv6:

```json
"addressFamily": "ipv6"
```

Payload preview must remain bounded and safely encoded.

Banner contents themselves should not automatically be copied into telemetry. Metadata about whether a banner was configured and sent is sufficient.

### Slice 24 - Build valid Mission Control envelopes

Publish to:

```text
POST /api/events
```

with:

```text
X-Mission-Control-Key: ...
```

The request envelope needs:

- unique event ID;
- event type;
- schema version;
- UTC occurrence timestamp;
- optional correlation ID;
- JSON object payload.

Use a small JSON library or carefully evaluate whether constructing this limited JSON shape manually is reasonable. Do not concatenate unescaped hostile payload text into JSON.

### Slice 25 - Best-effort telemetry delivery

Mission Control should have a short timeout.

Policy:

```text
publish succeeds -> continue
publish fails     -> optionally warn, continue
publish times out -> continue
```

Do not retry synchronously in the accept path.

If Mission Control latency becomes noticeable, add a tiny bounded queue or another non-blocking mechanism later.

---

## Phase 9 - Optional scanner classification

Only after the core program is stable.

Possible classifications:

- HTTP request;
- TLS ClientHello;
- SSH banner;
- Telnet negotiation;
- common bot/login probe;
- empty/banner-waiting connection;
- unknown binary payload.

Classification must be based only on the bounded bytes already received.

Never assume a payload is null-terminated.

Possible output:

```text
click! [connection #412] [port: 80] [remote: ...] [seen: 1] [probe: http]
```

Do not confuse tcpnoise's configured outbound banner with an inbound scanner banner or probe.

---

## Phase 10 - Rolling statistics / histogram

Optional terminal statistics:

```text
Connections: 1421

Ports:
23      932
80      301
2222    111
2323     77

Probe types:
empty   1012
http     221
telnet    91
ssh       34
other     63
```

Possible banner statistics later:

```text
Banners:
sent      384
failed      7
none      1030
```

Keep this in memory.

Do not introduce a database merely to count internet garbage.

---

# Suggested eventual source layout

Do not split files all at once. Move code only when a slice naturally creates a useful boundary.

A reasonable eventual layout is:

```text
07-tcpnoise/
├── Makefile
├── plan.md
├── include/
│   ├── banner.h
│   ├── connection.h
│   ├── listener.h
│   ├── logging.h
│   ├── seen.h
│   └── telemetry.h
└── src/
    ├── main.c
    ├── banner.c
    ├── connection.c
    ├── listener.c
    ├── logging.c
    ├── seen.c
    └── telemetry.c
```

This is a target, not a requirement.

Do not create a `.c`/`.h` pair for every five-line function just to make the tree look architectural.

Banner code only deserves its own module once loading/sending/configuration is large enough to justify one.

---

# Deployment notes

## Privileged ports

Ports below 1024 normally require additional privilege.

Avoid running the complete program as root long-term merely to bind ports such as 23 or 80.

Possible later deployment approaches include:

- Linux capabilities such as `CAP_NET_BIND_SERVICE`;
- an appropriate service manager configuration;
- binding before dropping privileges.

Keep this separate from the basic networking implementation.

## Firewall

Every watched port must also be allowed by the host firewall if public traffic should reach it.

IPv6 firewall rules must be verified separately where applicable.

## Logs

Use the operating system's rotation facilities for long-running public deployments.

## Banner files

Treat banner files as configuration.

Do not read them on every connection.

Keep banners bounded so a mistaken or huge file cannot cause excessive memory use or unexpectedly large writes.

Banner data may be arbitrary bytes, but the first implementation can focus on ordinary text files.

Avoid pretending to be a real service in ways that require protocol state. A static string such as:

```text
SSH-2.0-OpenSSH_8.9
```

or:

```text
Welcome to totally-real-telnet
```

is within scope; implementing an SSH or Telnet server is not.

## Mission Control secrets

Store API keys only in environment/service configuration or another secret mechanism.

Never place them in `plan.md`, source files, logs, banner files, or Git history.

---

# Known issues / review checklist

As development proceeds, keep checking:

- [ ] IPv4 and IPv6 both work on FreeBSD.
- [ ] IPv4 and IPv6 both work on Linux.
- [ ] Multiple ports can be watched simultaneously.
- [ ] Duplicate port arguments are handled.
- [ ] `SO_REUSEADDR` is enabled.
- [ ] IPv6 listeners use predictable `IPV6_V6ONLY` behavior.
- [ ] `SIGINT` shuts down cleanly.
- [ ] `SIGTERM` shuts down cleanly.
- [ ] All listener file descriptors are closed.
- [ ] Accepted client sockets are always closed.
- [ ] Payload bytes are never treated as a C string.
- [ ] Payload output remains terminal-safe.
- [ ] IPv6 addresses fit all buffers.
- [ ] Remote source port is captured.
- [ ] `snprintf()` truncation is handled.
- [ ] Address conversion errors are handled.
- [ ] Timestamp failures are handled.
- [ ] Seen-IP table exhaustion is quiet and predictable.
- [ ] Log failure does not stop connection handling.
- [ ] Missing banner files are handled silently or with nonfatal startup information.
- [ ] Banner files are loaded once rather than once per connection.
- [ ] Banner length is bounded.
- [ ] Partial banner writes are handled.
- [ ] `SIGPIPE` cannot unexpectedly terminate tcpnoise.
- [ ] Banner send failure does not stop connection handling.
- [ ] Mission Control failure does not stop connection handling.
- [ ] Mission Control API key is never logged.
- [ ] Telemetry payload is valid JSON even for hostile binary input.
- [ ] Long-running counters do not overflow quickly.
- [ ] No accidental dependency on Linux-only behavior.
- [ ] No accidental dependency on FreeBSD-only behavior.

---

# Definition of done

The expanded tcpnoise project is complete when one binary can be run like:

```sh
./tcpnoise 23 80 2222 2323
```

and:

- binds each requested port on IPv4 and IPv6 when available;
- watches all listeners concurrently;
- reports each accepted connection with the correct listening port;
- reports remote IPv4 or IPv6 address and source port;
- tracks per-IP seen counts;
- safely previews payload bytes;
- optionally sends a bounded configured per-port banner;
- writes per-port log files;
- shuts down cleanly;
- optionally emits bounded best-effort Mission Control events;
- builds cleanly with the project's warning flags on both FreeBSD and Linux.

Anything beyond that is a stretch goal, not an excuse to never call the project finished.
