# tcpnoise

A small TCP network-noise monitor written in C.

`tcpnoise` listens on one or more TCP ports and shows what connects: scanners, bots, probes, confused clients, and whatever other garbage happens to wander in from the network.

Basically, a terminal Geiger counter for unsolicited TCP traffic.

This is part of my FreeBSD/C learning lab, so it intentionally uses the underlying socket and system APIs directly rather than hiding them behind a networking library.

## Features

* Listen on up to 10 TCP ports at once
* Separate IPv4 and IPv6 listeners for every port
* Monitor all listeners with `poll()`
* Display the remote IP address and source port
* Track how many times each remote IP has been seen
* Maintain connection counts for each listening port
* Capture up to 256 bytes of the initial payload
* Escape non-printable payload bytes for readable output
* Detect payload, disconnects, timeouts, interruptions, and receive errors
* Append connection details to a separate log file for each port
* Keep log files open while the program is running
* Flush logs after every connection
* Optionally send randomized per-port banners
* Record which banner was sent, skipped, or failed
* Graceful `SIGINT` / `SIGTERM` shutdown
* Ignore `SIGPIPE` when clients disappear during a banner send

## Building

From the `07-tcpnoise` directory:

```sh
make
```

This builds:

```text
tcpnoise
```

The current Makefile compiles with:

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
./tcpnoise 2222 2323 8080
```

For each configured port, `tcpnoise` creates both an IPv4 and an IPv6 listening socket:

```text
Listening for noise on port: 2222 (IPv4)
Listening for noise on port: 2222 (IPv6)
Listening for noise on port: 2323 (IPv4)
Listening for noise on port: 2323 (IPv6)
```

Ports must be between `1` and `65535`.

A maximum of 10 port arguments may be supplied. Duplicate ports are ignored.

Binding privileged ports may require elevated privileges depending on the operating system.

## Connection output

For each accepted connection, `tcpnoise` displays:

* time
* connection number
* local TCP port
* address family
* remote IP address
* remote source port
* number of times that IP has been seen
* banner status
* initial payload status or contents

For example:

```text
[13:42:17] connection #12  TCP/2222  IPv4  203.0.113.42:51384  seen=3
           banner: NOTICE: YOUR PACKETS HAVE FAILED THE VIBE CHECK
           payload: GET / HTTP/1.1\r\nHost: example.com\r\n\r\n
```

IPv6 endpoints are formatted with brackets:

```text
[13:43:02] connection #13  TCP/2222  IPv6  [2001:db8::1234]:48121  seen=1
           banner: <none>
           payload: <timeout>
```

The `seen` counter is global while the program is running, so the same IP connecting to different monitored ports still increments the same counter.

Connection numbers are maintained separately for each configured port, with IPv4 and IPv6 connections contributing to the same port counter.

## Payload capture

After accepting a connection, `tcpnoise` performs a single `recv()` of up to 256 bytes.

The receive timeout is currently 250 milliseconds.

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

Other non-printable bytes are displayed as hexadecimal escapes:

```text
\x00
\xff
```

The short timeout is intentional. This program is interested in the initial thing a scanner throws at a socket, not in maintaining a long-running session.

## Logging

Each configured port gets its own append-only log file in the current working directory:

```text
2222.log
2323.log
8080.log
```

IPv4 and IPv6 connections for the same port share the same log.

Unlike earlier versions of `tcpnoise`, the log file is opened once when the program starts and remains open until shutdown.

Each connection is flushed to disk immediately after it is logged.

Console timestamps show only the time:

```text
[13:42:17]
```

Log entries contain the full local date and time:

```text
[2026-09-05 13:42:17] connection #12  TCP/2222  IPv4  203.0.113.42:51384  seen=3
- banner: NOTICE: YOUR PACKETS HAVE FAILED THE VIBE CHECK
- Payload: GET / HTTP/1.1\r\nHost: example.com\r\n\r\n
```

Banner failures and connections where no banner was selected are recorded as well:

```text
- banner: <none>
```

or:

```text
- banner: <send failed> some banner text
```

## Response banners

Each monitored port can optionally have its own banner pool.

Create a file named:

```text
<port>_banner.txt
```

For example:

```text
2222_banner.txt
```

Each line in the file is treated as a possible banner:

```text
NOTICE: YOUR PACKETS HAVE FAILED THE VIBE CHECK
WARNING: THE FIREWALL IS NOT MAD, JUST DISAPPOINTED
NOTICE: YOUR PACKETS HAVE BEEN FORWARDED TO /DEV/SHAME
SSH-2.0-OpenSSH_9.9
```

When a client connects and the banner pool is not empty, `tcpnoise` has a 75% chance of selecting and sending one of the lines.

The remaining 25% of connections receive no banner.

Selected banners are terminated with CRLF:

```text
\r\n
```

Banner sends correctly handle partial `send()` calls.

The complete banner plus CRLF must fit in the program's 256-byte send buffer.

If no `<port>_banner.txt` file exists, that port simply runs without banners.

### `banners.txt`

The included `banners.txt` file contains a collection of possible banners, including such highly professional network-administration messages as:

```text
ALERT: YOUR PACKETS HAVE FAILED THE VIBE CHECK
NOTICE: YOUR IP ADDRESS IS NOW THE SUBJECT OF A STRONGLY WORDED EMAIL
WARNING: THE FIREWALL IS NOT MAD, JUST DISAPPOINTED
```

`banners.txt` is **not loaded automatically**.

To use banners, copy the desired lines into the appropriate `<port>_banner.txt` file.

For example:

```sh
cp banners.txt 2222_banner.txt
./tcpnoise 2222
```

## Shutdown

Press `Ctrl+C` to stop the program.

`tcpnoise` handles `SIGINT` and `SIGTERM`, exits the polling loop, closes the listening sockets and log files, frees the banner pools, and prints connection totals:

```text
Shutdown signal caught, shutting down...
Port 2222 connection attempts: 37
Port 2323 connection attempts: 12
```

`SIGPIPE` is ignored so a client disappearing while a banner is being sent does not terminate the entire program.

## How it works

For every requested port, `tcpnoise` creates:

```text
IPv4 TCP listener
IPv6 TCP listener
```

The IPv6 sockets use `IPV6_V6ONLY`, allowing separate IPv4 and IPv6 sockets to bind the same port.

All listeners are placed in a `pollfd` array and monitored using `poll()`.

When a listener becomes readable:

1. Accept the connection.
2. Randomly select and optionally send a banner.
3. Set a 250 ms receive timeout.
4. Read up to 256 bytes.
5. Resolve the numeric remote IP and source port.
6. Update the in-memory seen-IP counter.
7. Print the connection to the terminal.
8. Append it to the port's log.
9. Close the client socket.

The program is intentionally single-threaded and handles accepted connections one at a time.

## Limits

Current compile-time limits include:

| Setting                |     Value |
| ---------------------- | --------: |
| Maximum port arguments |        10 |
| Listeners              |        20 |
| Initial payload        | 256 bytes |
| Tracked IP addresses   |      1000 |
| Listen backlog         |        64 |
| Receive timeout        |    250 ms |

The maximum listener count comes from two listeners per configured port: one IPv4 and one IPv6.

## What this is not

`tcpnoise` is a learning project and network curiosity tool, not a hardened honeypot or intrusion-detection system.

It is deliberately simple:

* single-threaded
* TCP only
* one initial `recv()` per connection
* fixed-size in-memory IP tracking
* simple text logs
* no log rotation
* no persistent IP database
* no authentication
* no privilege dropping
* no protocol emulation beyond sending a single banner
* `rand()` is used for banner selection

A client that connects and remains silent can occupy the single connection handler until the 250 ms receive timeout expires, so this is not designed to withstand serious load or hostile resource-exhaustion attempts.

That's fine for its actual job: watching the weird background radiation of the Internet.

## Why?

Because if you put an unused TCP port on the public Internet, somebody will eventually knock on it.

Then somebody else will send HTTP to it.

Then somebody will try Telnet credentials.

Then some broken scanner will send binary garbage intended for an industrial control system from 2007.

Then something will connect, say absolutely nothing, and disappear.

And I wanted to see that happen while learning sockets in C.
