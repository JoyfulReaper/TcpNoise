# tcpnoise

A small TCP network-noise monitor written in C.

`tcpnoise` listens on one or more TCP ports and shows what connects: scanners, bots, probes, confused clients, and whatever other garbage happens to wander in from the network.

Basically, a terminal Geiger counter for unsolicited TCP traffic.

This started as part of my FreeBSD/C learning lab, but it has turned into the most interesting actual program in the repository. The core collector is still deliberately small and low-level: POSIX sockets, `poll()`, SQLite, and the NATS C client.

There is also a small Windows tray companion, **TcpNoiseClicker**, which subscribes to live tcpnoise events over NATS and turns Internet background noise into actual background noise.

## Features

### tcpnoise collector

* Listen on up to 11 TCP ports at once
* Separate IPv4 and IPv6 listeners for every configured port
* Monitor all listening sockets with `poll()`
* Display remote IP address and source port
* Maintain per-port connection counters while the process is running
* Persist per-IP seen counts across restarts with SQLite
* Track first-seen and last-seen timestamps for remote IPs
* Persist connection totals by listening port and IP version
* Capture up to 4096 bytes of the initial payload
* Escape non-printable payload bytes for readable output
* Distinguish payload, disconnect, timeout, interruption, and receive-error states
* Append connection details to a separate text log for each port
* Keep log files open for the life of the process
* Flush logs after every connection
* Optionally send randomized per-port banners
* Record whether a banner was sent, skipped, or failed
* Publish live connection events to NATS
* Continue collecting traffic even if the NATS connection is unavailable
* Graceful `SIGINT` / `SIGTERM` shutdown
* Ignore `SIGPIPE` when clients disappear during a banner send
* Modular banner, logging, networking, database, and messaging code
* Include `check_stats.sh` for quick SQLite/log summaries

### TcpNoiseClicker

The `TcpNoiseClicker` directory contains a small .NET 10 Windows tray app that subscribes to the live NATS event stream.

Current behavior:

* Runs as a tray application with no main window
* Subscribes to `tcpnoise.connection`
* Plays a sound for each received connection event
* Shows today's event count plus the most recent remote IP and destination port in the tray tooltip
* Middle-truncates long IPv6 addresses so both ends remain visible
* Provides a tray-menu **Mute** option
* Uses persistent `seenCount` data from tcpnoise to distinguish first-seen addresses
* Uses special sounds for IPv6 and ports `420` / `42069`

Current sound precedence is:

```text
TCP/420 or TCP/42069  -> bong.wav
IPv6                  -> ipv6.wav
first-seen IPv4       -> new.wav
port-specific mapping -> configured port sound
everything else       -> click.wav
```

The special WAV files are optional except for `click.wav`; if an optional sound cannot be loaded, the app falls back to the normal click.

At the moment the port-specific sound dictionary is present but not populated.

## Architecture

The current live path looks like this:

```text
random Internet scanner
        |
        v
    tcpnoise
        |
        +--> terminal output
        |
        +--> per-port text logs
        |
        +--> SQLite
        |
        v
 tcpnoise.connection
        |
        v
      NATS
        |
        v
 TcpNoiseClicker
        |
        v
 click / bell / PEWWW / bong
```

NATS is being used as a live pub/sub path. The current subject is:

```text
tcpnoise.connection
```

This intentionally keeps the live clicker subject separate from the Mission Control `events.>` archive namespace for now.

## Building tcpnoise

The C collector requires:

* a C compiler
* SQLite 3
* `pkg-config` / `pkgconf`
* the NATS C client (`libnats`)

The Makefile obtains compiler and linker flags with:

```sh
pkg-config --cflags sqlite3
pkg-config --libs sqlite3
pkg-config --cflags libnats
pkg-config --libs libnats
```

The current warning/debug flags are:

```text
-Wall -Wextra -Wpedantic -g
```

### FreeBSD

Install SQLite and pkgconf:

```sh
pkg install sqlite3 pkgconf
```

Install the NATS C client so that the `libnats` pkg-config metadata is available.

On FreeBSD, if `pkg-config` cannot see a correctly installed library under `/usr/local`, make sure the normal FreeBSD pkg-config directory is on the search path:

```sh
export PKG_CONFIG_PATH=/usr/local/libdata/pkgconfig:$PKG_CONFIG_PATH
```

Verify the dependencies:

```sh
pkg-config --libs sqlite3
pkg-config --libs libnats
```

Then, from the `07-tcpnoise` directory:

```sh
make
```

### Debian / Ubuntu

Install the development dependencies:

```sh
sudo apt update
sudo apt install build-essential pkg-config libsqlite3-dev libnats-dev
```

Then:

```sh
make
```

Clean the build with:

```sh
make clean
```

## NATS configuration

The collector currently uses a hard-coded NATS server constant in `src/main.c`:

```c
#define NATS_SERVER "nats://10.99.0.1:4222"
```

Change that value for another environment.

The publisher connects once at startup. If the connection cannot be established, tcpnoise still continues its normal collection loop.

Each accepted connection is published as a JSON event on:

```text
tcpnoise.connection
```

The current event envelope looks like:

```json
{
  "EventId": "a4d5df50-ab0b-4815-8006-6fab08fd60ae",
  "EventType": "tcpnoise.connection",
  "Source": "tcpnoise",
  "SchemaVersion": 1,
  "OccurredAt": "2026-09-06T20:04:33Z",
  "ReceivedAt": "2026-09-06T20:04:33Z",
  "CorrelationId": null,
  "CausationId": null,
  "Payload": {
    "connectionNumber": 2,
    "listenPort": 2222,
    "ipVersion": 6,
    "remoteAddress": "2001:db8::1234",
    "remotePort": 59170,
    "seenCount": 2
  }
}
```

The payload currently contains connection metadata, not the captured network payload.

Keep NATS on a trusted/private network unless authentication and TLS are configured. The current development setup expects it to be reachable over the private network/VPN rather than exposed directly to the Internet.

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
./tcpnoise 21 23 80 443 2222 2323 3389 5432 8080 420 42069
```

For every configured port, tcpnoise creates both an IPv4 and IPv6 listener:

```text
Listening for noise on port: 2222 (IPv4)
Listening for noise on port: 2222 (IPv6)
```

Ports must be between `1` and `65535`.

A maximum of 11 unique port arguments may currently be supplied. Duplicate ports are ignored.

Binding ports below 1024 normally requires additional privileges.

For long-running Linux deployment, avoid running the whole process as root merely to bind privileged ports. Capabilities such as `CAP_NET_BIND_SERVICE` are a better option.

## Connection output

For every accepted connection, tcpnoise displays:

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

IPv4 and IPv6 connections to the same configured port contribute to the same in-process port counter.

The `seen` value comes from SQLite and therefore survives tcpnoise restarts.

## SQLite database

tcpnoise creates:

```text
tcpnoise.db
```

in the current working directory.

The database is opened once during startup and reused for the life of the process.

### `seen_ip`

Persistent remote-address history:

```text
seen_ip
-------
address        TEXT PRIMARY KEY
first_seen_utc TEXT NOT NULL
last_seen_utc  TEXT NOT NULL
seen_count     INTEGER NOT NULL
```

When an address is first seen, tcpnoise inserts it with a count of `1`.

Later connections update its last-seen timestamp and increment the count.

Example:

```sh
sqlite3 tcpnoise.db \
  'SELECT * FROM seen_ip ORDER BY seen_count DESC LIMIT 20;'
```

### `port_activity`

Persistent activity totals by listening port and IP version:

```text
port_activity
-------------
port             INTEGER
ip_version       INTEGER
first_seen_utc   TEXT
last_seen_utc    TEXT
connection_count INTEGER

PRIMARY KEY (port, ip_version)
```

Example:

```sh
sqlite3 -header -box tcpnoise.db \
  'SELECT port, ip_version, connection_count
   FROM port_activity
   ORDER BY connection_count DESC;'
```

This makes it easy to answer questions such as whether port 23 is attracting more garbage than 3389, or how much of a port's traffic is arriving over IPv6.

SQLite timestamps are stored in UTC.

Terminal and text-log timestamps use local time for readability.

Remote addresses and timestamps are bound as SQLite parameters rather than concatenated into SQL strings.

There is no fixed compile-time limit on the number of distinct IP addresses that can be remembered.

### Resetting one seen IP during testing

Because `seenCount == 1` is used by the Windows clicker for the first-seen sound, it can occasionally be useful to remove a test address:

```sh
sqlite3 tcpnoise.db \
  "DELETE FROM seen_ip WHERE address = '203.0.113.42';"
```

The next connection from that address will be inserted again with `seen_count = 1`.

Deleting a row from `seen_ip` does not modify the separate aggregate `port_activity` totals.

## Quick statistics

`check_stats.sh` provides a quick snapshot of the current database and logs:

```sh
./check_stats.sh
```

It currently displays:

* connection counts by port and IP version
* the 10 most recently seen IPs
* the 10 most frequently seen IPs
* recent IPv6 log entries, if any exist

For a live display on systems with `watch`:

```sh
watch ./check_stats.sh
```

## Payload capture

After accepting a connection, tcpnoise performs one bounded `recv()` of up to 4096 bytes.

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

The current implementation still performs a single `recv()`. Multi-read bounded capture is a possible future improvement.

## Logging

Each configured port gets its own append-only text log in the current working directory:

```text
23.log
80.log
2222.log
3389.log
```

IPv4 and IPv6 connections for the same port share the same log file.

Each log is opened once at startup and stays open until shutdown.

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

Each line is one candidate banner:

```text
NOTICE: YOUR PACKETS HAVE FAILED THE VIBE CHECK
WARNING: THE FIREWALL IS NOT MAD, JUST DISAPPOINTED
NOTICE: YOUR PACKETS HAVE BEEN FORWARDED TO /DEV/SHAME
SSH-2.0-OpenSSH_9.9
```

When a banner pool exists, tcpnoise has approximately a 75% chance of selecting and sending a banner.

The remaining approximately 25% of connections intentionally receive no banner.

Selected banners are terminated with:

```text
\r\n
```

Banner transmission handles partial `send()` calls and interrupted sends.

The encoded banner plus CRLF must fit inside the 256-byte banner-send buffer.

A missing `<port>_banner.txt` is normal and does not prevent the listener from starting.

### `banners.txt`

The included `banners.txt` contains a larger collection of possible responses, including highly professional network-administration messages such as:

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

## TcpNoiseClicker

`TcpNoiseClicker` is a Windows-only .NET 10 WinForms tray application.

It uses:

```text
NATS.Net 3.1.0
```

The current NATS endpoint and subject are hard-coded in `TrayApplicationContext.cs`:

```csharp
private const string NatsUrl = "nats://10.99.0.1:4222";
private const string Subject = "tcpnoise.connection";
```

Change the URL for another environment.

### Build and run

From `07-tcpnoise`:

```powershell
dotnet restore .\TcpNoiseClicker\TcpNoiseClicker.csproj
dotnet run --project .\TcpNoiseClicker\TcpNoiseClicker.csproj
```

The app has no normal main window. It appears in the Windows notification area.

Right-click the icon for:

```text
Mute
----
Exit
```

The tray tooltip tracks:

```text
<count today> | <last remote address> -> <listening port>
```

Long addresses are middle-truncated to stay within the small Windows tray-tooltip text limit.

### Sound files

The project currently includes:

```text
click.wav
new.wav
ipv6.wav
bong.wav
```

All WAV files in the project directory are copied to the output directory automatically.

`System.Media.SoundPlayer` is deliberately simple and works best with ordinary uncompressed PCM WAV files.

A useful ffmpeg conversion command is:

```powershell
ffmpeg -i input.mp3 -ac 1 -ar 44100 -c:a pcm_s16le output.wav
```

### Sound routing

Current precedence:

```text
1. port 420 / 42069 -> bong.wav
2. IPv6             -> ipv6.wav
3. seenCount == 1   -> new.wav
4. mapped port      -> port-specific SoundPlayer
5. otherwise        -> click.wav
```

This means a new IPv6 address gets the IPv6 sound rather than the first-seen sound.

A connection to port 420 or 42069 always gets the bong.

The first-seen sound therefore currently represents a new IPv4 address.

The app counts events even while muted; mute only suppresses audio.

## Testing NATS delivery

With the NATS CLI, listen for live tcpnoise events:

```sh
nats sub tcpnoise.connection --server nats://127.0.0.1:4222
```

Or, from another machine that can reach the broker:

```powershell
nats sub tcpnoise.connection --server nats://10.99.0.1:4222
```

You can also publish a fake event to test the clicker without waiting for a scanner:

```powershell
nats pub tcpnoise.connection '{"Payload":{"connectionNumber":999,"listenPort":23,"ipVersion":4,"remoteAddress":"203.0.113.123","remotePort":12345,"seenCount":1}}' --server nats://10.99.0.1:4222
```

## Shutdown

Press `Ctrl+C` to stop tcpnoise.

The collector handles `SIGINT` and `SIGTERM`, exits its polling loop, closes listeners, closes text logs, closes SQLite, frees banner pools, disconnects from NATS, and prints final per-port connection counts.

Example:

```text
Shutdown signal caught, shutting down...
Port 2222 connection attempts: 37
Port 3389 connection attempts: 12
```

`SIGPIPE` is ignored so a client disappearing during a banner send cannot unexpectedly terminate the process.

The Windows clicker exits from the tray menu and cancels its NATS subscription before disposing its sound players and tray icon.

## How tcpnoise works

For every requested port, tcpnoise creates:

```text
IPv4 TCP listener
IPv6 TCP listener
```

IPv6 sockets use `IPV6_V6ONLY`, allowing predictable separate IPv4 and IPv6 listeners on FreeBSD and Linux.

All listeners are placed into a `pollfd` array and monitored with `poll()`.

The current connection path is roughly:

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
        +--> update port/IP-version activity
        |
        +--> publish live NATS event
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

```text
07-tcpnoise/
├── Makefile
├── README.md
├── plan.md
├── banners.txt
├── check_stats.sh
├── include/
│   ├── banner.h
│   ├── database.h
│   ├── logging.h
│   ├── messaging.h
│   └── network.h
├── src/
│   ├── main.c
│   ├── banner.c
│   ├── database.c
│   ├── logging.c
│   ├── messaging.c
│   └── network.c
└── TcpNoiseClicker/
    ├── Program.cs
    ├── TcpNoiseEvent.cs
    ├── TrayApplicationContext.cs
    ├── TcpNoiseClicker.csproj
    ├── TcpNoiseClicker.slnx
    ├── click.wav
    ├── new.wav
    ├── ipv6.wav
    └── bong.wav
```

The C project intentionally avoids splitting every tiny helper into its own module.

The Windows client is kept separate from the collector and communicates only through NATS.

## Current limits

| Setting | Value |
| --- | ---: |
| Maximum port arguments | 11 |
| Maximum listeners | 22 |
| Initial payload capture | 4096 bytes |
| Banner send buffer | 256 bytes |
| Receive timeout | ~250 ms |
| NATS JSON buffer | 1024 bytes |
| Clicker tooltip text | capped at 63 characters |

Persistent seen-IP tracking and per-port/IP-version connection totals are stored in SQLite.

## What this is not

tcpnoise is a learning project and network-curiosity tool, not a hardened IDS or general-purpose honeypot framework.

It is deliberately simple:

* TCP only
* single-threaded collector
* one initial bounded `recv()` per accepted connection
* shallow one-line banner responses
* simple text logging
* SQLite for persistent counters/history
* Core NATS-style live publishing
* no internal log rotation
* no authentication
* no protocol emulation beyond the banner
* no packet capture
* no raw SYN monitoring
* no privilege dropping yet
* `rand()` is used for banner selection
* NATS URL and subject are currently compile/source configuration rather than runtime options

A client that connects and remains silent can occupy the single connection handler until the receive timeout expires.

That is acceptable for the current goal: observing ordinary unsolicited Internet background traffic without turning the project into a full honeypot framework.

## Planned ideas

Possible future work includes:

* Recent-events window in TcpNoiseClicker
* Port-specific sounds in TcpNoiseClicker
* Sound selection / volume controls later if they are worth the dependency
* Restore separate Mission Control publication while keeping `tcpnoise.connection` for ephemeral live subscribers
* Include captured payload previews in telemetry only after deciding how they should be bounded/escaped
* Simple scanner/probe classification
  * HTTP
  * TLS ClientHello
  * SSH
  * Telnet-style traffic
  * empty/banner-waiting connections
  * unknown binary payloads
* More rolling statistics
* terminal scrollback
* per-port terminal views
* configurable log/database/NATS paths
* bounded multi-read payload capture
* optional limited protocol emulation
* optional TLS handling for TLS-like probes

Fully asynchronous accepted-client handling remains deferred until real traffic demonstrates that the current synchronous receive path is actually a problem.

## Why?

Because if you put an unused TCP port on the public Internet, somebody will eventually knock on it.

Then somebody else will send HTTP to it.

Then somebody will try Telnet credentials.

Then some broken scanner will send binary garbage intended for an industrial control system from 2007.

Then something will connect, say absolutely nothing, and disappear.

And now, thanks to NATS and a Windows tray app, your workstation can make a different ridiculous noise when it happens.

I wanted to see that happen while learning sockets, POSIX APIs, FreeBSD, C, SQLite, and a little event-driven integration around the edges.
