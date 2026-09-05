# tcpnoise

A small TCP network-noise monitor written in C.

`tcpnoise` listens on one or more TCP ports and shows what connects: scanners, bots, probes, confused clients, and whatever other garbage happens to wander in from the network.

Basically, a terminal Geiger counter for unsolicited TCP traffic.

This is part of my FreeBSD/C learning lab, so it intentionally uses the underlying socket, POSIX, and SQLite APIs directly rather than hiding them behind a networking framework.

Of the experiments in this repository, `tcpnoise` is currently the one that has turned into the most interesting actual program.

## Features

* Listen on up to 10 TCP ports at once
* Separate IPv4 and IPv6 listeners for every port
* Monitor all listening sockets with `poll()`
* Display remote IP address and source port
* Maintain per-port connection counters
* Persist per-IP seen counts across restarts with SQLite
* Track first-seen and last-seen timestamps for remote IPs
* Capture up to 256 bytes of the initial payload
* Escape non-printable payload bytes for readable output
* Distinguish payload, disconnect, timeout, interruption, and receive-error states
* Append connection details to a separate text log for each port
* Keep log files open for the life of the process
* Flush logs after every connection
* Optionally send randomized per-port banners
* Record whether a banner was sent, skipped, or failed
* Graceful `SIGINT` / `SIGTERM` shutdown
* Ignore `SIGPIPE` when clients disappear during a banner send
* Modular banner, logging, networking, and database code

## Building

`tcpnoise` requires SQLite 3 and `pkg-config`.

### FreeBSD

Install the dependencies:

```sh
pkg install sqlite3 pkgconf
```

Then from the `07-tcpnoise` directory:

```sh
make
```

### Debian / Ubuntu

Install the required development packages:

```sh
sudo apt update
sudo apt install build-essential pkg-config libsqlite3-dev
```

Then:

```sh
make
```

The Makefile obtains SQLite compiler and linker flags through `pkg-config`.

The current warning flags are:

```text
-Wall -Wextra -Wpedantic -g
```

Clean the build with:

```sh
make clean
```

## Usage

```text
./tcpnoise <port> [port ...]
```

For example:

```sh
./tcpnoise 2222
```

Or monitor several ports at once:

```sh
./tcpnoise 23 80 2222 2323
```

For each configured port, `tcpnoise` creates both an IPv4 and IPv6 listening socket:

```text
Listening for noise on port: 2222 (IPv4)
Listening for noise on port: 2222 (IPv6)
Listening for noise on port: 2323 (IPv4)
Listening for noise on port: 2323 (IPv6)
```

Ports must be between `1` and `65535`.

A maximum of 10 port arguments may be supplied. Duplicate ports are ignored.

Binding ports below 1024 normally requires additional privileges.

For long-running deployment, avoid running the whole process as root merely to bind a privileged port. On Linux, capabilities such as `CAP_NET_BIND_SERVICE` are a better option.

## Connection output

For every accepted connection, `tcpnoise` displays:

* local time
* per-port connection number
* local TCP port
* address family
* remote IP address
* remote source port
* persistent seen count
* banner status
* initial payload status or contents

For example:

```text
[18:19:43] connection #12  TCP/2222  IPv4  203.0.113.42:51384  seen=37
           banner: NOTICE: YOUR PACKETS HAVE FAILED THE VIBE CHECK
           payload: GET / HTTP/1.1\r\nHost: example.com\r\n\r\n
```

IPv6 endpoints are formatted with brackets:

```text
[18:20:02] connection #13  TCP/2222  IPv6  [2001:db8::1234]:48121  seen=4
           banner: <none>
           payload: <timeout>
```

Connection numbers are maintained separately for each configured listening port.

IPv4 and IPv6 connections to the same configured port contribute to the same port counter.

The `seen` value comes from the SQLite database and therefore survives tcpnoise restarts.

## Persistent seen-IP database

`tcpnoise` creates:

```text
tcpnoise.db
```

in the current working directory.

The database is opened once during startup and reused for the lifetime of the process.

The current table is:

```text
seen_ip
-------
address        TEXT PRIMARY KEY
first_seen_utc TEXT NOT NULL
last_seen_utc  TEXT NOT NULL
seen_count     INTEGER NOT NULL
```

When an address is first seen, tcpnoise inserts it with a count of `1`.

Later connections update its last-seen timestamp and increment the existing count.

For example:

```sh
sqlite3 tcpnoise.db \
  'SELECT * FROM seen_ip ORDER BY seen_count DESC;'
```

might produce:

```text
127.0.0.1|2026-09-05 22:19:41|2026-09-05 22:19:43|2
::1|2026-09-05 22:19:48|2026-09-05 22:19:48|1
```

SQLite timestamps are stored in UTC.

Terminal and text-log timestamps use local time for readability.

Remote addresses and timestamps are bound as SQLite parameters rather than concatenated into SQL strings.

There is no longer a fixed compile-time limit on the number of distinct IP addresses that can be remembered.

## Payload capture

After accepting a connection, `tcpnoise` performs one bounded `recv()` of up to 256 bytes.

The receive timeout is currently approximately 250 milliseconds.

Possible results include:

```text
payload: GET / HTTP/1.1\r\n
payload: <peer closed>
payload: <timeout>
payload: <interrupted>
payload: <receive error>
```

Printable ASCII is displayed normally.

Common control characters are escaped:

```text
\n
\r
\t
```

Other non-printable bytes are shown as hexadecimal escapes:

```text
\x00
\xff
```

Payload data is handled as bounded bytes and is not assumed to be a NUL-terminated C string.

The short timeout is intentional. tcpnoise is interested in the first thing a scanner throws at the socket, not in maintaining long-running sessions.

## Logging

Each configured port gets its own append-only text log in the current working directory:

```text
23.log
80.log
2222.log
2323.log
```

IPv4 and IPv6 connections for the same port share the same log file.

Each log file is opened once during startup and remains open until shutdown.

Events are flushed after each connection.

Console timestamps show only local time:

```text
[18:19:43]
```

Log entries contain the full local date and time:

```text
[2026-09-05 18:19:43] connection #12  TCP/2222  IPv4  203.0.113.42:51384  seen=37
- banner: NOTICE: YOUR PACKETS HAVE FAILED THE VIBE CHECK
- Payload: GET / HTTP/1.1\r\nHost: example.com\r\n\r\n
```

Banner failures and intentionally silent connections are recorded too:

```text
- banner: <none>
```

or:

```text
- banner: <send failed> some banner text
```

For long-running deployments, use the operating system's log-rotation facilities rather than expecting tcpnoise to implement its own rotation.

## Response banners

Each monitored port can optionally have its own banner pool.

Create:

```text
<port>_banner.txt
```

For example:

```text
2222_banner.txt
```

Each line is treated as one candidate banner:

```text
NOTICE: YOUR PACKETS HAVE FAILED THE VIBE CHECK
WARNING: THE FIREWALL IS NOT MAD, JUST DISAPPOINTED
NOTICE: YOUR PACKETS HAVE BEEN FORWARDED TO /DEV/SHAME
SSH-2.0-OpenSSH_9.9
```

When a banner pool exists and contains candidates, tcpnoise has approximately a 75% chance of selecting and sending one.

The remaining approximately 25% of connections intentionally receive no banner.

Selected banners are terminated with:

```text
\r\n
```

Banner transmission handles partial `send()` calls and interrupted sends.

The encoded banner plus CRLF must fit inside the program's 256-byte banner-send buffer.

A missing `<port>_banner.txt` file is normal and does not prevent the listener from starting.

### `banners.txt`

The included `banners.txt` contains a larger collection of possible responses, including such highly professional network-administration messages as:

```text
ALERT: YOUR PACKETS HAVE FAILED THE VIBE CHECK
NOTICE: YOUR IP ADDRESS IS NOW THE SUBJECT OF A STRONGLY WORDED EMAIL
WARNING: THE FIREWALL IS NOT MAD, JUST DISAPPOINTED
```

`banners.txt` is not loaded automatically.

Copy the banners you want into the appropriate per-port file:

```sh
cp banners.txt 2222_banner.txt
./tcpnoise 2222
```

## Shutdown

Press `Ctrl+C` to stop tcpnoise.

The program handles `SIGINT` and `SIGTERM`, exits its polling loop, closes listeners, closes text logs, closes SQLite, frees banner pools, and prints final per-port connection counts.

Example:

```text
Shutdown signal caught, shutting down...
Port 2222 connection attempts: 37
Port 2323 connection attempts: 12
```

`SIGPIPE` is ignored so a client disappearing during a banner send cannot unexpectedly terminate the whole program.

## How it works

For every requested port, tcpnoise creates:

```text
IPv4 TCP listener
IPv6 TCP listener
```

IPv6 sockets use `IPV6_V6ONLY`, allowing predictable separate IPv4 and IPv6 listeners on FreeBSD and Linux.

All listeners are placed into a `pollfd` array and monitored with `poll()`.

The basic connection path is:

```text
listener becomes readable
        |
        v
      accept
        |
        +--> optionally choose/send banner
        |
        v
bounded recv with short timeout
        |
        v
resolve numeric remote endpoint
        |
        v
update persistent SQLite seen count
        |
        +--> terminal output
        |
        +--> per-port text log
        |
        v
close accepted socket
```

tcpnoise remains intentionally single-threaded.

Accepted clients are currently handled synchronously with a short receive timeout. Real public testing has not yet shown enough listener starvation to justify adding accepted-client sockets to the `poll()` state machine.

## Source layout

The program has been split into a few cohesive modules while keeping `main.c` responsible for orchestration:

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

The project intentionally avoids splitting every tiny helper into its own module.

## Current limits

| Setting                 |     Value |
| ----------------------- | --------: |
| Maximum port arguments  |        10 |
| Maximum listeners       |        20 |
| Initial payload capture | 256 bytes |
| Banner send buffer      | 256 bytes |
| Listen backlog          |        64 |
| Receive timeout         |   ~250 ms |

The listener limit comes from two listeners for every configured port: one IPv4 and one IPv6.

Persistent seen-IP tracking is handled by SQLite rather than a fixed-size in-memory array.

## What this is not

tcpnoise is a learning project and network-curiosity tool, not a hardened IDS or general-purpose honeypot framework.

It is deliberately simple:

* TCP only
* single-threaded
* one initial bounded `recv()` per accepted connection
* shallow one-line banner responses
* simple text logging
* SQLite for persistent seen-IP history
* no internal log rotation
* no authentication
* no protocol emulation beyond the banner
* no packet capture
* no raw SYN monitoring
* no privilege dropping yet
* `rand()` is used for banner selection

A client that connects and remains silent can occupy the single connection handler until the receive timeout expires.

That's acceptable for the current goal: observing ordinary unsolicited Internet background traffic without turning the project into a full honeypot framework.

## Planned ideas

Possible future work includes:

* Mission Control telemetry
* simple scanner/probe classification

  * HTTP
  * TLS ClientHello
  * SSH
  * Telnet-style traffic
  * empty/banner-waiting connections
  * unknown binary payloads
* optional SQLite connection-event history
* rolling statistics
* top/recent IPs
* terminal scrollback
* per-port terminal views
* configurable log/database paths

Fully asynchronous accepted-client handling remains deferred until real traffic demonstrates that the current 250 ms synchronous receive path is actually a problem.

## Why?

Because if you put an unused TCP port on the public Internet, somebody will eventually knock on it.

Then somebody else will send HTTP to it.

Then somebody will try Telnet credentials.

Then some broken scanner will send binary garbage intended for an industrial control system from 2007.

Then something will connect, say absolutely nothing, and disappear.

And I wanted to see that happen while learning sockets, POSIX APIs, FreeBSD, and C.
