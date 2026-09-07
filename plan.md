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

The project is now past the "can this work?" stage.

New complexity should earn its place by solving something observed in real use.

---

# Current status

As of 2026-09-07, tcpnoise is running against real public Internet traffic on two VPS sensors:

```text
New York
  Ubuntu
  tcpnoise collector
       |
       +---- WireGuard/private network ----+
                                          |
Phoenix                                   |
  Debian                                  |
  tcpnoise collector                      |
       |                                  |
       +---- WireGuard/private network ----+
                                          |
                                          v
                                        NATS
                                          |
                                          v
                                  TcpNoiseClicker
                                     on Windows
```

The collector has also been exercised on FreeBSD.

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
- separate banner, logging, networking, database, and messaging modules;
- `check_stats.sh` showing persistent port activity, top IPs, recent IPs, and IPv6 sightings;
- live `tcpnoise.connection` publication to NATS;
- NATS failure is nonfatal to normal collection;
- Windows `TcpNoiseClicker` live subscriber;
- first-seen, IPv6, and special-port sounds;
- successful live delivery from both NY and PHX into the same clicker;
- real public traffic observed on ports including 23, 80, 443, 8080, 2222, 2323, 3389, and 5432;
- deployment-specific banner pools, including port-23/Telnet-style bait.

SQLite is authoritative for persistent seen counts and persistent port activity.

The synchronous accepted-client path is still intentionally simple. Real traffic has produced large connection bursts, but there is not yet enough evidence that the collector itself needs a full event-driven accepted-client state machine.

IPv6 listeners are confirmed working end-to-end.

A controlled IPv6 connection from another WireGuard/public-IPv6 host has been observed successfully. A genuinely unsolicited public IPv6 scanner has **not** yet been observed.

---

# What changed since the last plan

The previous immediate goal was:

> Publish each connection to NATS and make a desktop computer click like a Geiger counter whenever the Internet touches the VPS.

That phase is now working.

The live path is:

```text
Internet
   |
   v
tcpnoise
   |
   +--> terminal
   +--> per-port logs
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
click / PEWWW / IPv6 sound / bong
```

The architecture has also naturally become multi-sensor:

```text
tcpnoise NY  -----+
                  |
                  +----> tcpnoise.connection ----> NATS ----> subscribers
                  |
tcpnoise PHX -----+
```

That exposed the next two real problems:

1. Events do not yet identify which sensor produced them.
2. Huge scanner bursts can temporarily overwhelm `SoundPlayer` in the desktop clicker.

Those are now more important than inventing another collector feature.

---

# Immediate next goals

## 1. Add sensor/site identity to connection events

The same NATS subject now receives events from multiple collectors.

Today, a subscriber cannot tell whether a connection was seen by NY or PHX.

Add one small identity field to the published event.

Possible values:

```text
ny
phx
```

Prefer runtime/deployment configuration over compiling separate binaries for each location.

A simple environment variable is likely enough:

```text
TCPNOISE_SENSOR=ny
TCPNOISE_SENSOR=phx
```

Possible payload shape:

```json
{
  "connectionNumber": 12,
  "listenPort": 23,
  "ipVersion": 4,
  "remoteAddress": "203.0.113.42",
  "remotePort": 51384,
  "seenCount": 3,
  "sensor": "phx"
}
```

Requirements:

- one tcpnoise binary should work at every site;
- missing sensor configuration should behave predictably;
- sensor identity must be bounded;
- sensor identity must be JSON escaped correctly;
- messaging failure must remain nonfatal;
- adding the field must not affect SQLite/log collection;
- update the Windows event model to consume the field;
- display/use the sensor only where useful.

A later option is to use the sensor identity for distinct sounds, but do not make that part of the first slice.

---

## 2. Fix TcpNoiseClicker burst-audio choking

A real Internet burst already demonstrated the problem.

One NY scanner produced roughly:

```text
1932 connections in about 12 seconds
```

That is far beyond what `System.Media.SoundPlayer.Play()` should be expected to represent one-for-one in real time.

Observed behavior:

- NATS/event counting continued;
- sound playback temporarily choked;
- playback recovered without restarting the app.

The fix should preserve telemetry and event counts while making audio best-effort.

Preferred shape:

```text
NATS event
   |
   +--> always record/count
   |
   v
bounded sound queue
   |
   v
one controlled playback path
```

Rules:

- never drop the actual received event/count;
- sound is allowed to be lossy during an extreme burst;
- do not accumulate minutes of delayed clicking;
- keep the normal rapid Geiger-counter feel;
- avoid blocking the NATS subscription loop;
- bound memory;
- recover automatically after a burst;
- keep mute semantics simple.

Possible implementation:

- bounded `Channel<T>`;
- dedicated audio worker;
- drop/coalesce excess audio notifications when full;
- serialize or deliberately throttle playback.

Do not add a large audio framework unless `SoundPlayer` remains inadequate after the bounded queue approach.

---

## 3. Restore Mission Control publication separately

Keep the live clicker subject:

```text
tcpnoise.connection
```

ephemeral and simple.

Separately restore Mission Control/history publication using something like:

```text
events.tcpnoise.connection
```

or another bridge that matches Mission Control conventions.

Preferred architecture:

```text
                       tcpnoise.connection
tcpnoise ---> NATS ------------------------------> live clicker
      |
      +----> events.tcpnoise.connection ---------> Mission Control / archive
```

Alternative:

```text
tcpnoise ---> tcpnoise.connection ---> bridge ---> events.tcpnoise.connection
```

Choose whichever keeps tcpnoise least coupled to Mission Control.

Absolute rule:

> Mission Control failure must never interfere with collection or the live clicker path.

---

# Multi-sensor experiments

Running NY and PHX at the same time creates a useful new class of experiment.

Questions worth answering:

- how quickly is a fresh public IP discovered?;
- which ports attract traffic first?;
- do the same scanners hit both regions?;
- how different are repeat-offender populations?;
- does one region receive more Telnet, HTTP, RDP, PostgreSQL, or other noise?;
- do scanner bursts happen at both sites at similar times?;
- which site gets the first genuinely unsolicited IPv6 connection?;
- do banner responses change follow-up behavior?;
- do some scanners repeatedly target one destination port while others fan out?

Do not hard-code comparison logic into the collector.

The collector should publish/store facts. Comparison belongs in scripts, SQLite queries, Mission Control, or a later reporting tool.

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

For now, keep each sensor's SQLite database local.

Do not merge NY and PHX persistence into a distributed database merely because multiple sensors now exist.

Cross-site comparison can happen downstream.

---

# Lightweight dashboard

`check_stats.sh` is already useful enough to count as a dashboard.

Current views:

- persistent port/IP-version leaderboard;
- top 10 most-seen IPs;
- top 10 most recently seen IPs;
- quick IPv6 log check.

Running it independently on NY and PHX is currently enough for quick comparison.

Do not replace this with ncurses until the shell version actually becomes limiting.

Possible small improvements later:

- top source-IP -> destination-port combinations;
- per-port top talkers;
- connections in the last hour/day;
- first-seen scanner count;
- rough connections/minute;
- sensor name in the heading.

---

# NATS / Geiger counter phase — WORKING

Current subject:

```text
tcpnoise.connection
```

Current module:

```text
include/messaging.h
src/messaging.c
```

Current behavior:

```text
NATS available       -> publish event
NATS unavailable     -> continue
publish fails        -> continue
collector shutdown   -> release NATS resources
```

The desktop subscriber uses live Core NATS semantics.

There is no replay backlog when the clicker reconnects.

That is intentional for the audible Geiger-counter use case.

## Remaining NATS work

- add sensor/site identity;
- decide whether additive schema changes require a schema-version increment;
- restore/bridge Mission Control publication;
- confirm reconnect behavior under real network interruption;
- keep message size bounded;
- keep JSON escaping correct;
- avoid raw payload publication until privacy/bounding rules are settled.

---

# TcpNoiseClicker — WORKING, WITH ONE REAL LIMIT

Current behavior:

- Windows tray app;
- subscribes to `tcpnoise.connection`;
- records today's event count;
- remembers the most recent remote endpoint/listening port;
- mute support;
- IPv6 sound;
- first-seen IPv4 sound;
- special `420` / `42069` bong;
- normal click fallback.

Current sound precedence:

```text
420 / 42069
    |
    v
IPv6
    |
    v
first-seen IPv4
    |
    v
port-specific mapping
    |
    v
default click
```

## Known problem

Large scanner bursts can overwhelm direct `SoundPlayer.Play()` calls.

This is now proven by real traffic rather than theoretical concern.

Fix with bounded/controlled audio playback while preserving all received event counts.

Possible later ridiculousness:

- sensor-specific left/right sound;
- subtly different click pitch by location;
- angry clunk for 3389;
- port-specific sounds;
- optional recent-event window;
- volume control.

Do not add these before burst handling and sensor identity.

---

# Response banners

Per-port banner pools are working.

File format:

```text
<port>_banner.txt
```

Each line is one possible one-line response.

A shared generic pool can be symlinked:

```sh
for p in 21 23 80 443 420 2222 2323 3389 5432 8080 42069; do
    ln -s banners.txt "${p}_banner.txt"
done
```

Individual ports can then replace the symlink with specialized pools.

Port 23 is a natural place for Telnet/router/IoT/login-style banners.

This is deployment configuration, not yet a requirement to commit every production banner file into the repository.

Future banner experiments should stay bounded and shallow.

Do not silently evolve banner support into a full protocol emulator.

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

Real examples have already included:

- HTTP-like probes;
- TLS ClientHello-like data on unexpected ports;
- silent/banner-waiting Telnet connections;
- high-rate scanner bursts;
- ordinary peer-close probes.

Classification becomes more useful once multi-read capture is good enough that TCP segmentation does not make trivial classifiers misleading.

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

The specialized port-23 banner pool is useful evidence that this could become interesting, but one-line banners are still enough for now.

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

A future version could show separate NY/PHX pages once sensor identity is propagated cleanly downstream.

---

# FreeBSD support

FreeBSD remains a first-class learning/testing target.

The collector has been built and exercised there.

The NATS C client may need to be built manually when a convenient package is unavailable.

A working source is the fork:

```text
https://github.com/JoyfulReaper/nats.c
```

The README contains the concrete FreeBSD build/install/pkg-config steps.

Keep the collector's POSIX assumptions conservative enough to remain usable on both FreeBSD and Linux.

Avoid Linux-only APIs unless there is a clear guarded fallback.

---

# Long-running Linux deployment

Current public sensors run under Linux.

For privileged ports, prefer a narrow capability over running the entire process as root:

```sh
sudo setcap cap_net_bind_service=+ep ./tcpnoise
```

For eventual unattended operation, a systemd unit is reasonable.

Possible service properties:

```text
WorkingDirectory=<tcpnoise directory>
Restart=on-failure
Environment=TCPNOISE_SENSOR=phx
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
```

A service unit is useful after runtime configuration is cleaned up.

Do not rush into systemd-specific code inside tcpnoise itself.

---

# Configuration cleanup — NEAR TERM

Several values are still source/build/deployment configuration.

Candidates for runtime configuration:

- NATS URL;
- NATS subject;
- sensor/site identity;
- database path;
- log directory;
- banner directory;
- receive timeout;
- maybe banner probability.

Do not build a giant configuration system.

Prefer:

1. a few environment variables;
2. simple CLI flags only when they become worth the parsing complexity.

Sensor identity is the first runtime configuration worth adding because multi-sensor deployment already exists.

---

# Cleanup / hardening backlog

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

- understand NATS connection ownership;
- clean up every resource;
- bound message size;
- correctly JSON-escape strings;
- never log credentials;
- rely on library reconnect behavior rather than inventing a retry framework;
- add sensor identity;
- decide schema-version policy for additive fields;
- restore Mission Control publication separately from the live clicker subject.

## Clicker

- bounded audio queue;
- clean cancellation/disposal of audio worker;
- no event loss just because sound is dropped;
- verify tray counts under synthetic and real bursts;
- optionally include sensor in tooltip later.

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
- [x] Debian VPS testing
- [x] Real public IPv4 traffic
- [x] Controlled end-to-end IPv6 connection
- [ ] Observe real unsolicited IPv6 traffic

## Persistence / stats

- [x] Persistent seen-IP count
- [x] First/last seen
- [x] Persistent port/IP-version activity
- [x] Top IPs
- [x] Recent IPs
- [x] Quick IPv6 log check
- [ ] Source-IP -> destination-port activity
- [ ] Schema migration/version strategy
- [ ] Finalize-result hardening

## Payload

- [x] Explicit length
- [x] Escaped display
- [x] 4096-byte bounded buffer
- [ ] Bounded multi-read
- [ ] Optional bounded HTTP body capture

## Banners

- [x] Optional per-port banner files
- [x] Random banner selection
- [x] Intentional silent percentage
- [x] Partial-send handling
- [x] Deployment use of shared symlinked banner pool
- [x] Deployment use of specialized port-23 banner pool
- [ ] Optional bounded multi-step interaction

## NATS / Geiger counter

- [x] Add NATS C dependency
- [x] Add messaging module
- [x] Connect once at startup
- [x] Publish `tcpnoise.connection`
- [x] Make NATS-unavailable nonfatal
- [x] Make publish failure nonfatal
- [x] Clean shutdown
- [x] Desktop live subscriber
- [x] Play Geiger click
- [x] First-seen sound
- [x] IPv6 sound
- [x] Special 420 / 42069 sound
- [x] Receive events from two collectors
- [ ] Add sensor/site identity
- [ ] Fix burst audio choking
- [ ] Optional per-port sounds
- [ ] Restore/bridge Mission Control publication
- [ ] Optional JetStream capture/history if still useful

## Multi-sensor

- [x] NY sensor live
- [x] PHX sensor live
- [x] Both reach the same private NATS broker
- [x] Clicker receives traffic from both
- [ ] Include sensor identity in event payload
- [ ] Compare scanner overlap between sites
- [ ] Compare per-port distributions
- [ ] Compare burst patterns
- [ ] Observe first real unsolicited IPv6 scanner

## Downstream

- [ ] Mission Control event path restored
- [ ] Optional .NET API
- [ ] Keep all downstream failures isolated from tcpnoise

---

# Definition of core done

The core tcpnoise project is already substantially proven.

One binary can run like:

```sh
./tcpnoise 23 80 443 2222 2323 3389 5432 8080
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
- publish connection events over NATS;
- continue operating when NATS is unavailable;
- shut down cleanly;
- run on FreeBSD and Linux.

The desktop companion can:

- subscribe live;
- count events;
- identify first-seen addresses;
- distinguish IPv6;
- make the workstation audibly react to Internet garbage.

The same live NATS path now accepts events from more than one public sensor.

That means the project has moved into a new phase.

The next work is not about proving tcpnoise can function.

It is about making a small distributed Internet-noise experiment increasingly useful without wrecking the simplicity that made it fun.

The immediate priorities are:

> **Add sensor identity, make the clicker survive insane bursts gracefully, and restore Mission Control publication without coupling the collector to downstream systems.**

After that, let the garbage decide what deserves to exist next.
