# Troubleshooting — the persistent event log

Every node (`main_controller`, `c3_sidecar`, `s3_display`, `round_display_2424`) keeps a
small history of what it's done — boot messages, state changes, connectivity changes, RFID
scans, run results, driver errors — and that history survives a reboot or a full power cycle.
That's what you pull up when a box misbehaves somewhere out of reach of a laptop and you need
to know what actually happened before you got there.

The implementation is `diagnostics::EventLog`
([firmware/shared/include/diagnostics/event_log.h](../firmware/shared/include/diagnostics/event_log.h)),
shared by all four build environments.

## How it works

- Each node keeps the last **40 events** in a RAM ring buffer, mirrored to NVS flash (via the
  same Arduino `Preferences` API `HighscoreService` already used for the top-10 list) after
  every single event. Once the 41st event happens, the oldest one is overwritten.
- Every entry has a sequence number (never reused, keeps counting up across reboots), a
  `millis()` timestamp (**resets to 0 every boot** — it tells you time-since-boot, not
  wall-clock time; there's no RTC on most of these nodes), a short tag (e.g. `game`, `web`,
  `rfid`, `highscore`, `sidecar`, `display`, `speaker`, `touch`), and a message capped at 39
  characters (longer messages are silently truncated — the full untruncated text still goes to
  Serial at the moment it happens).
- Each node's log lives in its own NVS namespace (`evtlog_main`, `evtlog_side`, `evtlog_disp`),
  so the four boxes never mix histories — you always retrieve the log from the node you're
  actually investigating.
- Logging a event still prints it to Serial exactly as before (`[tag] message`) — nothing
  about the existing serial console output changed, it's just now also durable.

### What is (and isn't) logged

Discrete, meaningful events go through the logger: boot/ready messages, Wi-Fi/ESP-NOW link
up/down, game state transitions, puzzle start/order, run completion, RFID scans, highscore
loads, and driver init failures (I2S, GT911 touch, ESP-NOW).

The 5-second heap/loop-time diagnostic heartbeat in `AppController::tick()` and the
display node's `/api/status` poll (every 750 ms–1.5 s) are **deliberately left Serial-only**.
Every `logf()` call rewrites the whole log blob to flash; wiring up something that fires
several times a second would wear the flash for no real troubleshooting benefit (a live
Serial monitor already shows that noise fine). Keep that rule in mind if you add new call
sites — see "Adding a new log call site" below.

## Retrieving the log

### Serial command (works on every node)

Open a serial monitor on the node in question and type `log` (or `logs`) then Enter:

```bash
pio device monitor -b 115200 -p COMx
```

```
> log
---- event log: 12/40 entries ----
#0    t+     412ms [main      ] Chronolab main controller ready
#1    t+     889ms [input_panel] MCP23017 ready, 3 encoders configured
#2    t+    1042ms [highscore ] 3 entries loaded from NVS
...
---- end event log ----
```

This works identically on `main_controller`, `c3_sidecar`, `s3_display`, and
`round_display_2424` — the command reader (`EventLog::pollSerialCommand()`) is polled once per
`tick()`/`loop()` on all four.

### HTTP endpoint (main controller only)

`main_controller` already runs a small JSON API on its Wi-Fi access point
(`Chronolab-X13`, see [network_config.h](../firmware/shared/include/config/network_config.h)),
so its log is also reachable without a serial cable — handy since it's the node most likely to
be sealed inside the box when something goes wrong:

```bash
curl http://192.168.4.1/api/logs
```

```json
{
  "events": [
    { "seq": 0, "timestampMs": 412, "tag": "main", "message": "Chronolab main controller ready" },
    { "seq": 1, "timestampMs": 889, "tag": "input_panel", "message": "MCP23017 ready, 3 encoders configured" }
  ]
}
```

The sidecar and displays don't run a web server (the sidecar never joins a Wi-Fi network — it
talks ESP-NOW only, peer-to-peer over the same radio; the displays are Wi-Fi *clients* of the
main controller's AP, not servers themselves), so the serial command is the only way to pull
their logs.

## Adding a new log call site

Call `eventLog_.logf(tag, fmt, ...)` (a `diagnostics::EventLog&` is either already a member of
the class you're in, or reachable — `AppController::eventLog_`, `SidecarController::eventLog()`,
or the file-scope `eventLog` in `display/src/main.cpp`). It's printf-style and also prints to
Serial, so it's a drop-in replacement for a `Serial.println`/`Serial.printf` call:

```cpp
eventLog_.logf("game", "run complete, score=%u s", lastScoreSeconds_);
```

Keep the message under 39 characters where practical — anything longer is truncated in what
gets persisted (full text still goes to Serial). Don't wire up anything that fires more than
roughly once every few seconds — each call is a full flash rewrite of the log blob; leave
high-frequency/periodic output as a plain `Serial.print` instead, the same as the existing
5-second diagnostic heartbeat and the display's connectivity poll.
