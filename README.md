# tcpnoise

A small TCP network-noise monitor written in C.

`tcpnoise` listens on one or more TCP ports and shows what connects: scanners, bots, probes, confused clients, and whatever other garbage happens to wander in from the network.

Basically, a terminal Geiger counter for unsolicited TCP traffic.

This started as part of my FreeBSD/C learning lab, but it has turned into the most interesting actual program in the repository. The collector is deliberately small and low-level: POSIX sockets, `poll()`, SQLite, and the NATS C client.

There is also a small Windows tray companion, **TcpNoiseClicker**, which subscribes to live tcpnoise events over NATS and turns Internet background noise into actual background noise.

## Features

### tcpnoise collector

* Listen on up to 11 TCP ports at once
* Listen on IPv4 and IPv6
* Optionally bind a collector to one specific numeric IPv4 or IPv6 address with `--bind`
* Use `TCPNOISE_SENSOR` to give a collector an explicit sensor/site/network name
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
* Include sensor identity, banner information, and bounded payload data in NATS events
* Continue collecting traffic even if NATS is unavailable
* Graceful `SIGINT` / `SIGTERM` shutdown
* Ignore `SIGPIPE` when clients disappear during a banner send
* Modular banner, logging, networking, database, and messaging code
* Include `check_stats.sh` for quick SQLite/log summaries
* Build and run on FreeBSD and Linux

### TcpNoiseClicker

The `TcpNoiseClicker` directory contains a small .NET 10 Windows tray app that subscribes to the live NATS event stream.

Current behavior includes:

* Runs as a tray application
* Subscribes to `tcpnoise.connection`
* Plays a sound for each received connection event
* Tracks received tcpnoise events
* Shows sensor, remote endpoint, destination port, IP version, banner, payload, and seen count in the history view
* Provides a tray-menu **Mute** option
* Uses persistent `seenCount` data from tcpnoise to distinguish first-seen addresses
* Uses special sounds for IPv6 and ports `420` / `42069`
* Continues counting events while muted

The project also contains `AndroidNoiseClicker`, an Android-side NATS subscriber experiment.

## Architecture

A single-sensor deployment looks like this:

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
        +-------------------+
        |                   |
        v                   v
 TcpNoiseClicker      other consumers
```

Multiple collectors can publish to the same private NATS broker:

```text
Internet / DN42 / Yggdrasil
       |        |        |
       v        v        v
    sensor   sensor   sensor
       \        |        /
        \       |       /
         +------v------+
                NATS
                 |
          +------+------+
          |             |
          v             v
   TcpNoiseClicker   future consumers
```

The live subject is:

```text
tcpnoise.connection
```

This intentionally keeps the live clicker subject separate from the Mission Control `events.>` archive namespace for now.

### Multi-sensor deployments

Each collector includes a sensor identifier in its published events.

By default tcpnoise uses the local hostname. Override it with the `TCPNOISE_SENSOR` environment variable:

```sh
TCPNOISE_SENSOR=overlay-dn42 \
  ./tcpnoise --bind 172.20.220.54 22 23 80 443
```

Example logical sensor names:

```text
clanker
scopecreep
noisemaker
overlay-dn42
overlay-ygg
```

IPv4 and IPv6 instances may intentionally use the same sensor name when they represent the same logical network/site. The event's `ipVersion` field distinguishes the address family.

For example, these can both publish as `overlay-dn42`:

```text
172.20.220.54
fdf0:e12c:5528::54
```

while a Yggdrasil listener on the same host can publish as:

```text
overlay-ygg
```

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

Install the normal build dependencies:

```sh
pkg install sqlite3 pkgconf cmake
```

#### Building libnats on FreeBSD

If a packaged NATS C client is not available or convenient, my fork of `nats.c` can be used as a known-working source for building `libnats` on FreeBSD:

```text
https://github.com/JoyfulReaper/nats.c
```

Clone it:

```sh
git clone https://github.com/JoyfulReaper/nats.c.git
cd nats.c
```

`tcpnoise` only needs the normal NATS client, so NATS Streaming can be omitted:

```sh
cmake -S . -B build -DNATS_BUILD_STREAMING=OFF
cmake --build build
```

Install it under `/usr/local`:

```sh
sudo cmake --install build --prefix /usr/local
```

On a working FreeBSD setup, this installed the `libnats` pkg-config metadata under:

```text
/usr/local/lib/pkgconfig/libnats.pc
```

FreeBSD ports/packages may also use `/usr/local/libdata/pkgconfig`, so including both is convenient:

```sh
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/libdata/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
```

Verify the dependencies:

```sh
pkg-config --modversion libnats
pkg-config --cflags --libs libnats
pkg-config --cflags --libs sqlite3
```

Then, from the repository root:

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

## Usage

```text
./tcpnoise [--bind IPv4-or-IPv6-address] <port> [port ...]
```

Ports must be between `1` and `65535`.

A maximum of 11 unique port arguments may currently be supplied. Duplicate ports are ignored.

### Default wildcard behavior

Without `--bind`, tcpnoise retains its original behavior and creates both IPv4 and IPv6 wildcard listeners for each configured port:

```sh
./tcpnoise 2222
```

Example output:

```text
Listening for noise on port: 2222 (IPv4)
Listening for noise on port: 2222 (IPv6)
```

Monitor several ports at once:

```sh
./tcpnoise 21 23 80 443 2222 2323 3389 5432 8080 420 42069
```

### Binding to one address

Use `--bind` when a host has several networks or addresses and tcpnoise should listen only on one of them.

IPv4 example:

```sh
TCPNOISE_SENSOR=overlay-dn42 \
  ./tcpnoise --bind 172.20.220.54 21 22 23 25 80 443 445 3389 5900 8080 8443
```

IPv6 example:

```sh
TCPNOISE_SENSOR=overlay-dn42 \
  ./tcpnoise --bind fdf0:e12c:5528::54 21 22 23 25 80 443 445 3389 5900 8080 8443
```

Yggdrasil example:

```sh
TCPNOISE_SENSOR=overlay-ygg \
  ./tcpnoise --bind 203:b4fc:fbe5:6a84:238d:69a1:dcbf:ed54 \
  21 22 23 25 80 443 445 3389 5900 8080 8443
```

`--bind` currently accepts a numeric IPv4 or IPv6 address.

When `--bind` is present:

* tcpnoise determines the address family from the supplied address
* only that address family is created
* the listener is bound only to the requested address
* an unavailable/non-local address causes the bind to fail
* tcpnoise does not silently fall back to a wildcard listener

This makes it suitable for network-specific sensors on multi-homed systems.

### Verifying a bind

On Linux:

```sh
sudo ss -lntp | grep tcpnoise
```

A correctly bound sensor should show the requested local address rather than `0.0.0.0` or `[::]`.

## Binding privileged ports on Linux

Ports below 1024 normally require elevated privileges.

Avoid running the entire collector as root just so it can bind ports such as 21, 22, 23, 25, 80, or 443.

For a manually launched binary, one option is:

```sh
sudo setcap cap_net_bind_service=+ep ./tcpnoise
getcap ./tcpnoise
```

Expected output looks like:

```text
./tcpnoise cap_net_bind_service=ep
```

Replacing/rebuilding the executable may require applying the file capability again.

For systemd deployments, granting the capability through the service unit is usually cleaner.

## Linux systemd deployment

A reusable systemd template works well for running multiple independent tcpnoise instances on the same Linux host.

The following deployment pattern was tested with separate DN42 IPv4, DN42 IPv6, and Yggdrasil IPv6 instances.

Create:

```text
/etc/systemd/system/tcpnoise@.service
```

Example:

```ini
[Unit]
Description=TCPNoise sensor (%i)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=joyfulreaper
Group=joyfulreaper
WorkingDirectory=/home/joyfulreaper/tcpnoise

EnvironmentFile=/etc/tcpnoise/%i.conf

ExecStart=/bin/sh -c 'exec /home/joyfulreaper/tcpnoise/tcpnoise --bind "$TCPNOISE_BIND" $TCPNOISE_PORTS'

Restart=on-failure
RestartSec=5

# Permit privileged listener ports without running tcpnoise as root.
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

Create the configuration directory:

```sh
sudo mkdir -p /etc/tcpnoise
```

Then create one environment file per instance.

### DN42 IPv4 example

```ini
# /etc/tcpnoise/dn42-v4.conf
TCPNOISE_SENSOR=overlay-dn42
TCPNOISE_BIND=172.20.220.54
TCPNOISE_PORTS="21 22 23 25 80 443 445 3389 5900 8080 8443"
```

### DN42 IPv6 example

```ini
# /etc/tcpnoise/dn42-v6.conf
TCPNOISE_SENSOR=overlay-dn42
TCPNOISE_BIND=fdf0:e12c:5528::54
TCPNOISE_PORTS="21 22 23 25 80 443 445 3389 5900 8080 8443"
```

### Yggdrasil IPv6 example

```ini
# /etc/tcpnoise/ygg-v6.conf
TCPNOISE_SENSOR=overlay-ygg
TCPNOISE_BIND=203:b4fc:fbe5:6a84:238d:69a1:dcbf:ed54
TCPNOISE_PORTS="21 22 23 25 80 443 445 3389 5900 8080 8443"
```

Reload systemd:

```sh
sudo systemctl daemon-reload
```

Start one instance for testing:

```sh
sudo systemctl start tcpnoise@dn42-v4.service
sudo systemctl status --no-pager tcpnoise@dn42-v4.service
```

Verify the listeners:

```sh
sudo ss -lntp | grep tcpnoise
```

Enable tested instances at boot:

```sh
sudo systemctl enable \
  tcpnoise@dn42-v4.service \
  tcpnoise@dn42-v6.service \
  tcpnoise@ygg-v6.service
```

Check:

```sh
systemctl is-enabled \
  tcpnoise@dn42-v4.service \
  tcpnoise@dn42-v6.service \
  tcpnoise@ygg-v6.service
```

### Firewall notes

Opening a tcpnoise port globally is usually unnecessary on a multi-network sensor host.

Prefer firewall rules scoped to:

* the intended interface
* the intended local destination address
* the configured tcpnoise port set

For example, a DN42 sensor should not accidentally make the same ports reachable from a management or clearnet interface merely because the process exists.

The listener bind and firewall policy should both enforce the intended exposure.

### Shared working-directory caveat

Instances using the same `WorkingDirectory` also share:

```text
tcpnoise.db
<port>.log files
<port>_banner.txt files
```

NATS events remain distinguishable through `TCPNOISE_SENSOR`, but local SQLite statistics and per-port log files can combine traffic from several instances.

For example, `overlay-dn42` and `overlay-ygg` can publish distinct NATS identities while still updating the same local `tcpnoise.db` if both services use:

```text
WorkingDirectory=/home/joyfulreaper/tcpnoise
```

If per-sensor local persistence is required, give each instance a separate working/data directory.

## NATS configuration

The collector currently uses a hard-coded NATS server constant in `src/main.c`:

```c
#define NATS_SERVER "nats://10.99.0.1:4222"
```

Change that value for another environment.

The current deployment keeps NATS on a trusted/private network rather than exposing port `4222` directly to the public Internet.

The publisher connects once at startup. If the connection cannot be established, tcpnoise still continues its normal collection loop.

Each accepted connection is published on:

```text
tcpnoise.connection
```

The sensor name is:

1. `TCPNOISE_SENSOR`, when set to a non-empty value
2. otherwise the system hostname
3. `unknown` only if hostname lookup fails

A current event looks roughly like:

```json
{
  "EventId": "a4d5df50-ab0b-4815-8006-6fab08fd60ae",
  "EventType": "tcpnoise.connection",
  "Source": "tcpnoise",
  "SchemaVersion": 1,
  "OccurredAt": "2026-10-10T23:45:45Z",
  "ReceivedAt": "2026-10-10T23:45:45Z",
  "CorrelationId": null,
  "CausationId": null,
  "Payload": {
    "sensor": "overlay-dn42",
    "connectionNumber": 2,
    "listenPort": 22,
    "ipVersion": 6,
    "remoteAddress": "fd42:42:42:252::9",
    "remotePort": 49326,
    "seenCount": 1,
    "banner": null,
    "bannerSent": false,
    "payloadLength": 0,
    "payload": ""
  }
}
```

The event contains bounded connection/payload information suitable for live consumers such as TcpNoiseClicker.

Messaging is deliberately best effort:

```text
NATS available   -> publish event
NATS unavailable -> continue collecting
publish fails    -> continue collecting
```

A downstream outage should not stop socket handling, SQLite persistence, or local logging.

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

Example:

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

IPv4 and IPv6 connections to the same configured port contribute to the same in-process port counter when they are handled by the same process.

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

This makes it easy to compare activity on ports and between IPv4/IPv6.

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

Running the script independently on multiple sensors makes it easy to compare how quickly different public IPs are discovered and which exposed ports attract the most traffic.

Remember the shared-working-directory caveat when multiple systemd instances use the same SQLite file.

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

IPv4 and IPv6 connections for the same port/process share the same log file.

Multiple systemd instances also share those files when configured with the same working directory.

Each log is opened once at startup and stays open until shutdown.

Events are flushed after each connection.

Console timestamps show local time:

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

Copy it for one port:

```sh
cp banners.txt 2222_banner.txt
```

or symlink a shared pool to several ports:

```sh
for p in 21 23 80 443 420 2222 2323 3389 5432 8080 42069; do
    ln -s banners.txt "${p}_banner.txt"
done
```

A port can still have a dedicated pool. For example, port 23 can use a Telnet/router/IoT-style banner list while other ports use `banners.txt`.

If `23_banner.txt` is currently a symlink and you want to replace it with a real file:

```sh
rm 23_banner.txt
$EDITOR 23_banner.txt
```

Restart tcpnoise after changing banner files because banner pools are loaded at startup.

## TcpNoiseClicker

`TcpNoiseClicker` is a Windows-only .NET 10 WinForms tray application.

It uses NATS.Net and subscribes to:

```text
tcpnoise.connection
```

The current NATS endpoint and subject are configured in `TrayApplicationContext.cs`.

### Build and run

From the repository root:

```powershell
dotnet restore .\TcpNoiseClicker\TcpNoiseClicker.csproj
dotnet run --project .\TcpNoiseClicker\TcpNoiseClicker.csproj
```

The app appears in the Windows notification area.

The history view can be used to inspect incoming events from multiple sensors, including their sensor name, remote endpoint, listening port, IP version, seen count, banner, and captured payload.

### Sound files

The project includes sound files such as:

```text
click.wav
new.wav
ipv6.wav
bong.wav
```

`System.Media.SoundPlayer` works best with ordinary uncompressed PCM WAV files.

A useful ffmpeg conversion command is:

```powershell
ffmpeg -i input.mp3 -ac 1 -ar 44100 -c:a pcm_s16le output.wav
```

### Sound routing

Place overrides under `sounds/` beside the executable (`AppContext.BaseDirectory`).
The directory is optional; each node and each individual override is optional too.

```text
TcpNoiseClicker/
  click.wav                 # required ultimate fallback
  new.wav                   # optional
  ipv6.wav                  # optional
  bong.wav                  # optional
  sounds/
    dn42/
      all.wav               # every event from this sensor
    ygg/
      all.wav
    ports/
      22.wav                # global port override
      80.wav
      443.wav
    clanker/                # matches the event's Sensor
      click.wav
      new.wav
      ipv6.wav
      bong.wav
      ports/
        22.wav              # node port override
        443.wav
    scopecreep/
      bong.wav
      ports/
        80.wav
```

For every event, resolution uses this exact order:

1. `sounds/<Sensor>/all.wav`
2. `sounds/<Sensor>/ports/<ListenPort>.wav`
3. `sounds/ports/<ListenPort>.wav`
4. `sounds/<Sensor>/<semantic sound filename>`
5. `<AppContext.BaseDirectory>/<semantic sound filename>`
6. `<AppContext.BaseDirectory>/click.wav`

The semantic filename is selected with these rules, in order:

```text
1. port 420 / 42069 -> bong.wav
2. IPv6             -> ipv6.wav
3. seenCount == 1   -> new.wav
4. otherwise        -> click.wav
```

A sensor's `all.wav` beats every other override and semantic rule, giving that
entire sensor/network one recognizable sound. Port overrides beat every semantic
rule, including IPv6, first-seen, and bong.
Missing, inaccessible, corrupt, or unplayable optional sounds silently fall through
to the next candidate. A missing/bad required `click.wav` still reports a startup
error; if it fails during playback, the clicker retains its system-beep fallback.

Loaded sounds and missing/failed lookups are cached by full path, distinguishing
sensor, port, and sound filename (including `all.wav`). Playback failures are
retired from the cache.
Restart the app after adding or changing overrides. All cached players are disposed
after the single sound consumer stops.

Sensor names must be safe single Windows directory names. Paths, traversal,
invalid filename characters, device names, trailing dots/spaces, and the reserved
name `ports` skip node overrides and still use global sounds. Names match Windows
filesystem casing. Checked-in files beneath `TcpNoiseClicker/sounds/**` are copied
to build and publish output with their directory structure preserved.

Mute suppresses audio without stopping event reception/counting. History and
tooltips continue to update independently of playback.

Run the sound-resolution tests without audio hardware or a NATS connection:

```powershell
dotnet test .\TcpNoiseClicker\TcpNoiseClicker.slnx
```

### Burst audio

Real Internet traffic can arrive in large bursts.

Audio notification should be treated as a convenience; event reception/counting is the important path.

The clicker uses a bounded queue of 32 events and one consumer with synchronous
playback. When the queue is full, new audio notifications are dropped while all
received events still update counts and history. Override loading and resolution run
on that consumer, so override loading does not block NATS reception.

## Testing NATS delivery

With the NATS CLI, listen for live tcpnoise events:

```sh
nats sub tcpnoise.connection --server nats://127.0.0.1:4222
```

Or, from another machine that can reach the private broker:

```powershell
nats sub tcpnoise.connection --server nats://10.99.0.1:4222
```

You can also publish a fake event to test a consumer without waiting for a scanner.

For real end-to-end testing, connect to one of the configured listener ports from a host that can reach the sensor:

```sh
nc -vz 172.20.220.54 22
```

Then verify that the event arrives with the expected sensor name.

## Shutdown

Press `Ctrl+C` to stop a foreground tcpnoise process.

The collector handles `SIGINT` and `SIGTERM`, exits its polling loop, closes listeners, closes text logs, closes SQLite, frees banner pools, disconnects from NATS, and prints final per-port connection counts.

Example:

```text
Shutdown signal caught, shutting down...
Port 2222 connection attempts: 37
Port 3389 connection attempts: 12
```

`SIGPIPE` is ignored so a client disappearing during a banner send cannot unexpectedly terminate the process.

systemd stops use `SIGTERM`, so the same graceful shutdown path is used by:

```sh
sudo systemctl stop tcpnoise@dn42-v4
```

## How tcpnoise works

Without `--bind`, for every requested port tcpnoise creates:

```text
IPv4 TCP listener
IPv6 TCP listener
```

IPv6 sockets use `IPV6_V6ONLY`, allowing predictable separate IPv4 and IPv6 listeners on FreeBSD and Linux.

With `--bind`, the supplied numeric address determines the address family and tcpnoise creates only that family, bound to that address.

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

The main collector is split into a small set of modules rather than one file per helper:

```text
.
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
├── TcpNoiseClicker/
└── AndroidNoiseClicker/
```

The Windows and Android clients are separate from the C collector and communicate through NATS.

## Current limits

| Setting | Value |
| --- | ---: |
| Maximum port arguments | 11 |
| Maximum listeners without `--bind` | 22 |
| Maximum listeners with `--bind` | 11 |
| Initial payload capture | 4096 bytes |
| Banner send buffer | 256 bytes |
| Receive timeout | ~250 ms |

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
* no internal privilege-dropping mechanism
* `rand()` is used for banner selection
* NATS URL and subject are currently compile/source configuration rather than runtime options

A client that connects and remains silent can occupy the single connection handler until the receive timeout expires.

That is acceptable for the current goal: observing ordinary unsolicited background traffic without turning the project into a full honeypot framework.

## Planned ideas

Possible future work includes:

* Separate per-sensor local database/log directories for multi-instance deployments where local statistics must remain isolated
* Make NATS endpoint/subject runtime configurable
* Improve TcpNoiseClicker burst audio with bounded/serialized playback
* More port-specific sounds and UI controls where useful
* Restore separate Mission Control publication while keeping `tcpnoise.connection` for ephemeral live subscribers
* Decide how much captured payload data should be retained downstream
* Track source-IP -> destination-port activity
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
* configurable log/database paths
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

Put another sensor somewhere else — or bind separate collectors to DN42 and Yggdrasil — and now you can compare which network finds each listener first.

I wanted to see that happen while learning sockets, POSIX APIs, FreeBSD, C, SQLite, and a little event-driven integration around the edges.
