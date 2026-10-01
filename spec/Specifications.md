# Chronolab Puzzlebox — Hardware Specifications & Confirmed Quirks

Hardware-specific facts, confirmed by testing on the actual physical units, that aren't
obvious from a datasheet and cost real debugging time to find once already. Unlike
[components.md](components.md) (what's physically installed) and
[puzzlebox_hw.md](puzzlebox_hw.md) (how everything is wired/driven in general), this file
is a per-module list of "this exact unit behaves like X, not like the datasheet/default
assumption" — so the next person (human or Claude) doesn't have to re-derive it from
scratch via another multi-hour debug-page session.

**Update this file whenever a debug session (via `/debug` or otherwise) nails down a
hardware quirk like this.** State what was assumed, what's actually true, and how it was
confirmed, with file:line pointers to the fix.

---

## Main Controller (ESP32 DevKit V1)

### TM1638 LED&KEY button numbering is not sequential

**Assumed:** button `Sn` (silkscreen 1-8) reads back as bit `n-1` of the button mask
(`S1`→bit0, `S2`→bit1, … `S8`→bit7) — the naive result of the TM1638's
`bit(2i)`/`bit(2i+1)` KS-line scan.

**Actual:** this board's K1 legs are `S1-S4` and K2 legs are `S5-S8` — i.e. two rows of
four, not interleaved per KS line. Bit *n* of the scan corresponds to `S1,S5,S2,S6,S3,S7,
S4,S8` in raw order, not `S1..S8`.

**Fix:** `InputPanelService::tm1638ReadButtons()` remaps raw scan bits to true `S(n+1)`
numbering before returning, so every consumer (`TetrisPuzzle`, `VibrationalCipherPuzzle`,
the `/debug` page) can assume sequential numbering.
[firmware/main/src/devices/input_panel_service.cpp](../firmware/main/src/devices/input_panel_service.cpp)
(`tm1638ReadButtons()`, `kRawBitForButton` table).

**Confirmed:** via `/debug`, pressing each of S1-S8 individually and reading the raw mask.

**If you swap in a different TM1638 clone board**, re-verify this via `/debug` before
trusting button numbers — this is a per-board wiring fact, not a TM1638-family constant.

### Encoder RGB LEDs: red/green channels are swapped

**Assumed:** `EncoderLedChannels.r`/`.g` in `encoder_hardware_config.h` pointed at the
PCA9685 channel actually wired to that color (sequential `{0,1}`, `{2,3}`, `{4,5,6}` per
encoder).

**Actual:** the red and green legs are physically wired to swapped PCA9685 channels on all
three encoders — setting "red" lit green and vice versa.

**Fix:** `kEncoderLedChannels` swaps the `r`/`g` channel numbers per encoder (`{1,0,...}`,
`{3,2,...}`, `{5,4,6,...}`).
[firmware/main/src/devices/encoder_hardware_config.h](../firmware/main/src/devices/encoder_hardware_config.h).
Blue (encoder 3 only) was already correct. Brightness multipliers
(`kBicolorRedMultiplier`/`kBicolorGreenMultiplier`) follow the channel, not the color name,
so they stayed correct once the channel numbers were swapped — no separate brightness bug.

**Confirmed:** via `/debug`, setting each encoder to pure red/green individually and
observing the opposite color light up, on all three encoders.

### Color sensor (TCS3200) blocks the main loop hard when absent

**Issue (not a wiring bug, a design gap):** `ColorSensorService::poll()` does up to 12
`pulseIn()` calls at an 8ms timeout each (~96ms worst case) every 250ms. With the sensor
**Not connected** (per [components.md](components.md)), `pulseIn()` times out fully almost
every cycle — burning ~38% of main-loop time on a sensor that isn't there.

**Fix:** after `kNoSignalStreakForBackoff` (4) consecutive `NoSignal` reads, the poll
interval backs off from 250ms to `kBackoffIntervalMs` — self-healing the instant a
real signal appears (any successful read resets the streak).
[firmware/main/src/devices/color_sensor_service.{h,cpp}](../firmware/main/src/devices/color_sensor_service.h).
No behavior change while the sensor is actually connected.

**Follow-up (measured):** the `[diag] worstLoopUs` serial line showed a ~102ms loop stall in
every 5s window even with the 3000ms backoff — once network latency to the display was fixed,
this was the dominant remaining source of Setup-screen encoder lag. Backoff raised to
15000ms; `worstLoopUs` dropped to ~4.9ms (the one remaining ~102ms hit is within the first
~1s after boot, before the streak reaches 4). Use the `[diag]` line (5s interval, main
controller serial) as the first check for any "screen feels laggy" report.

### Rotary encoder `value` counts quadrature edges, not detents

`InputPanelService::EncoderState::value` increments per A/B edge (`kQuadratureDelta`), and a
mechanical detent on these encoders fires 4 edges. Setup, puzzle selection and highscore
letters originally applied a full step per raw edge, so one click could apply up to 4x its
step in separate loop iterations (jumpy/imprecise). `AppController::kEncoderCountsPerDetent
= 4` now divides the delta, and the baseline advances only by the consumed multiple so a
partial turn carries over instead of being lost. Any new discrete-stepping encoder consumer
must do the same.
[firmware/main/src/app/app_controller.{h,cpp}](../firmware/main/src/app/app_controller.h).
The analog-style consumers (Spectral Tuner, Living Interval) read `value` continuously and
are unaffected.

### WiFi AP power-save

`WiFi.setSleep(false)` is called right after `WiFi.mode(WIFI_MODE_APSTA)` in
`AppController::begin()` — default ESP32 modem-sleep adds latency/jitter to the display's
poll link otherwise.
[firmware/main/src/app/app_controller.cpp](../firmware/main/src/app/app_controller.cpp).

### `/debug` hardware test page

`GET /debug` on the main controller (`http://192.168.4.1/debug` on the AP) is a
self-contained page, independent of game state, for testing every row in
[components.md](components.md) individually: TM1638 buttons (live, correctly numbered) +
its own digit/LED test pattern, all 3 encoders (live value/button + per-LED color test),
vibration motor pulse, servo open/close, RFID/RTC/color-sensor/hidden-sensor live readback,
WS2812 matrix test pattern, audio cue bus test (tone/cue — only audible via the display
board), and a **main display test pattern** (color bands + live touch-coordinate readout +
a dot that follows your finger, plus a raw I2C byte dump and the GT911's own configured
resolution — this is what found every touch-related bug below). Source:
[firmware/main/src/web/debug_page.h](../firmware/main/src/web/debug_page.h),
[firmware/main/src/app/app_controller.cpp](../firmware/main/src/app/app_controller.cpp)
(`handleDebugStatusGet`, `handleDebugTestJson`, `applyPendingDebugTest`).

Debug-test actions are queued from the AsyncWebServer request handler and applied on the
next `AppController::tick()` (main loop task), not executed directly in the handler — the
handler runs on its own FreeRTOS task and several actions (encoder LEDs) hit the shared I2C
bus that `poll()` also uses.

---

## Main Display Node — ESP32-8048S050C (`s3_display`)

### GT911 touch: point register offset was wrong by one register

**Assumed (GT911 datasheet, naive reading):** point 1's `track_id` at `0x8150`, X at
`0x8151-0x8152`, Y at `0x8153-0x8154`.

**Actual (confirmed against Espressif's own `esp_lcd_touch_gt911` driver, as used by
[mr-sven/esp32-8048S050C](https://github.com/mr-sven/esp32-8048S050C), a reference project
for this exact board):** `track_id` is at `0x814F`, **X low/high = `0x8150`/`0x8151`**, **Y
low/high = `0x8152`/`0x8153`**, strength low/high = `0x8154`/`0x8155`. No `swap_xy` /
`mirror_x` / `mirror_y` needed on this board.

**What went wrong first:** reading from `0x8151` (one register too late) made the "X"
formula (`point[1]|point[2]<<8`) actually read X's high byte + Y's low byte as one 16-bit
chimera — which *happened* to vary smoothly enough with one physical axis (touch height) to
look like a working, if swapped, axis, while the true second axis was never read at all and
stayed near 0 regardless of touch position. This cost several rounds of "axis swap" /
"needs a scale factor" hypotheses before the actual register offset was found — **the
lesson: when one axis works cleanly and the other is flat/near-zero no matter what, suspect
a byte-alignment bug in the read, not a swap or a missing scale factor.**

**Fix:**
[firmware/display/src/touch_service.cpp](../firmware/display/src/touch_service.cpp) —
`kRegPoint1 = 0x8150`, `rawX = point[0]|point[1]<<8`, `rawY = point[2]|point[3]<<8`.

**Also fixed in the same pass** (still live in the code, keep these):
- I2C address auto-probe (`0x5D` then fallback `0x14`) in `TouchService::begin()` —
  `INT` is grounded rather than connected on this board (confirmed via the same reference
  driver's own comment: "interrupt pin was falsely routed to GND... so its 0x5D"), which is
  *why* 0x5D is correct here, not floating/random per `puzzlebox_hw.md` 6.1's more general
  warning.
- `readRegs()` returns whether it actually got all requested bytes; `readState()` treats a
  short read as "no new data" instead of trusting a partially-stale buffer.
- GT911's own configured output resolution (registers `0x8048-0x804B`) is read once at
  `begin()` and exposed via `configXMax()`/`configYMax()` — confirmed `800`/`480` (correct,
  matches the panel) on this unit, which is what ruled out "misconfigured resolution" as the
  cause and pointed back at the register-offset bug instead.

**Confirmed:** via `/debug`'s touch test — a dot that should track the finger 1:1, plus a
raw 8-byte point dump per touch, across dedicated top/bottom/left/right/corner presses.

### Display poll interval had drifted to 200ms (validated value: 750ms/1500ms)

**Assumed (by the code, before this fix):** `GameView`'s own `kPollIntervalMs = 200` was
safe.

**Actual:** [puzzlebox_hw.md](puzzlebox_hw.md) §7.3 already documented 750ms
online / 1500ms offline-retry as **"validated — do not tighten without testing; the display
used to flap between online and offline"** — `GameView` had drifted to 200ms (4x tighter),
reproducing exactly that flapping. The round display's separate poll loop in
`main.cpp` still used the correct validated values the whole time, which is how this was
caught (see it work correctly, then find `GameView` not using the same constants).

**Fix:** restored `kOnlinePollIntervalMs = 750` / `kOfflineRetryIntervalMs = 1500`, plus a
`kDisconnectAfterFailures = 2` debounce (a single dropped/slow poll no longer flashes the
"no connection" screen — only 2 consecutive failures do).
[firmware/display/src/game_view.cpp](../firmware/display/src/game_view.cpp)
(`GameView::poll()`).

### WiFi STA power-save

Same fix as the main controller: `WiFi.setSleep(false)` right after `WiFi.mode(WIFI_STA)`
in `main.cpp`, before `WiFi.begin()`.
[firmware/display/src/main.cpp](../firmware/display/src/main.cpp).

### Status feed: direct UART link main controller -> display (replaces the WebSocket push)

**Why:** the 750ms HTTP poll made Setup-screen encoder feedback visibly laggy next to the
TM1638. A WebSocket push (tried first) got closer but still carried WiFi latency, and
`links2004/WebSockets`' `loop()` does a **blocking** TCP connect (`WEBSOCKETS_TCP_TIMEOUT` =
5000ms) — called from the main loop it froze rendering/touch for seconds on any hiccup. A
FreeRTOS task fixed the freeze but not the latency. Wired UART is **hardware-confirmed
instant**; the WS client/server code was removed.

**Wiring (3 wires):** main controller GPIO33 (TX) -> display GPIO12 (RX); main GPIO32 (RX)
<- display GPIO13 (TX); GND to GND (the display's SPI header has no GND — take it from
another GND point, e.g. the UART0 header). GPIO12/13 are the TF/SD-card SPI SCLK/MISO on
the header labelled 19/11/12/13; the firmware never initialises the SD slot, so they're
free **as long as no SD card is inserted**. Do NOT use 17/18 (I2S audio), 19/20 (touch I2C),
or RX0/TX0 (GPIO43/44 — the CH340 USB-serial used for flashing/logs).

**Protocol:** `AppController::pushGameStatusOverUart()` writes the same JSON as `GET
/api/game`, one line per snapshot (`println`), every 10ms at 921600 baud. The display frames
on `\n`; a torn line fails `deserializeJson` and the next line resyncs. HTTP `GET /api/game`
(750ms) stays as the connectivity-health fallback; touch, logs and `/debug` stay on WiFi.

**Gotchas found on hardware:**
- The Arduino-ESP32 UART RX ring buffer defaults to **256 bytes**, smaller than one status
  line — ~2/3 of lines arrived torn. `mainUart_.setRxBufferSize(8192)` before `begin()`
  fixed it (now ~96% of lines OK; the rest are dropped harmlessly).
- Render once per drain (newest line wins), not once per received line.
- Diagnostics: the display prints `[uart] bytes= linesOk= linesBad=` every 5s, but on
  **`Serial0`** (UART0 / CH340, COM4) — see the next entry.
[firmware/main/src/app/app_controller.cpp](../firmware/main/src/app/app_controller.cpp)
(`pushGameStatusOverUart`),
[firmware/display/src/game_view.cpp](../firmware/display/src/game_view.cpp)
(`GameView::pollMainUart`).

**Follow-ups (all hardware-confirmed):**
- Sending identical snapshots 100x/s made the display re-render constantly (puzzles froze).
  The main controller now only sends when anything besides `seq` changed, plus a 250ms
  heartbeat.
- The display's remaining HTTP poll blocked its loop 160-250ms per request (measured with
  `worstPollUs` in the `[uart]` line), freezing screen and sound in Living Interval. The
  display now has **no WiFi at all**: status in and touches out (`{"touch":N}` lines,
  `AppController::pollDisplayUart`) both go over the wire, and "no connection" means no
  UART line for 1s. Display flash use dropped from 1.31MB to 0.71MB.
- `round_display_2424` already failed to build before this (WiFi101/WiFiEspAT library
  conflict) — pre-existing, not caused by the UART work.

### Status snapshots carry a `seq`; the display drops stale ones

Two transports (UART/WS push and HTTP poll) racing means an older snapshot can arrive after
a newer one and stomp it back (seen as the Setup time flickering 15 <-> 30 on a fast
encoder turn). `buildGameStatusJson()` stamps a monotonic `seq`;
`GameView::applyGameStatus()` ignores anything below the highest `seq` already applied.

### GY-91 IMU via a Trinket M0 sensor node

The GY-91 stays on a Trinket M0 with short I2C wires instead of moving onto the main
controller's bus: a long I2C run there would share the bus with the MCP23017/PCA9685 the
encoders depend on. The Trinket streams JSON lines at 50 Hz, 115200 baud, Trinket pin 4 (TX)
→ main GPIO27 (`ImuService`, UART2, RX only), plus common GND. Live readout on `/debug`.

- **Chips (probed by ID register):** genuine MPU-9250 (`WHO_AM_I` 0x71) at 0x68, AK8963
  magnetometer (WIA 0x48) at 0x0C via bypass, BMP280 (id 0x58) at 0x76. Not a MPU-6500 fake.
- **Accelerometer reads ~1.7 g at rest** with the range correctly set to ±4 g (register
  readback confirmed) — an offset/sensitivity error in this chip, not firmware. Not yet
  calibrated; the tilt maze sidesteps it by measuring tilt relative to a baseline captured at
  puzzle start (re-captured on any encoder button press).
- 50 Hz is plenty for tilt/orientation/shake/compass/pressure games, but too slow to catch
  knocks (few-ms spikes). A knock game should detect on the Trinket at high rate and send an
  event.
- Trinket USB ID `239A:801E` = Arduino sketch (CircuitPython would be `801F` with a
  `CIRCUITPY` drive). Flash with `pio run -e trinket_imu -t upload --upload-port COMx`.

### Tilt maze axis mapping

`TiltMazePuzzle` maps accel X (sign −1) to screen right and accel Y (sign **+1**) to screen
down — Y was inverted on the first try and flipped after hardware testing. If the GY-91 is
remounted, re-tune `kScreenXAxis/Sign` and `kScreenYAxis/Sign` in
[tilt_maze_puzzle.cpp](../firmware/main/src/puzzles/tilt_maze_puzzle.cpp). Gain
(14 cells/s² per g) was confirmed to feel right.

### `Serial` on the display is native USB-CDC, not the CH340

`boards/esp32-8048S050C.json` sets `ARDUINO_USB_CDC_ON_BOOT=1`, so `Serial.printf` output
goes to the S3's native USB (GPIO19/20 — which this board wires to touch), not to COM4.
COM4 only shows the ROM boot banner. Use `Serial0` for anything you want to read over COM4.

---

## Build tooling (applies to every environment)

### `patch_tcpip_adapter.py` non-ASCII output hangs every build on Windows

**Symptom:** `pio run` appears to hang forever right after "Running tcpip_adapter.h patch
script..." with no error — or crashes deep in PlatformIO's own output-echo thread with
`UnicodeEncodeError`.

**Cause:** the pre-build script printed `✓`/`⚠` characters. PlatformIO echoes captured
subprocess output through a background thread using the Windows console's codepage
(cp1252, not UTF-8 by default) — those characters aren't encodable there, so the thread
dies, the subprocess's stdout pipe is never drained again, and the build eventually
deadlocks once that pipe's buffer fills.

**Fix:** replaced with plain ASCII (`OK:`, `WARNING:`) in
[patch_tcpip_adapter.py](../patch_tcpip_adapter.py). **Never print non-ASCII characters
from any PlatformIO `extra_scripts` on this project** — it will reproduce this hang
regardless of which script does it.

### A killed build mid-run can corrupt PlatformIO's own Python env

Stopping a `pio run` (e.g. Ctrl+C, task kill) while PlatformIO is mid-self-update can leave
its bundled `penv` with a broken `certifi` install and leftover `~latformio`-prefixed
dist-info directories, breaking `pio` entirely (`ImportError: cannot import name 'where'
from 'certifi'`). Fix if it happens: `python -m pip install --ignore-installed --no-deps
--no-cache-dir certifi` inside `%USERPROFILE%\.platformio\penv\Scripts\python.exe`, then
delete any `~latformio*` leftovers in `penv\Lib\site-packages`. Prefer letting a build run
to completion over killing it once flashing/compiling has started.

---

## Not yet root-caused

Nothing outstanding as of 2026-09-27 — every symptom raised in this debugging session
(TM1638 numbering, encoder colors, vibration/servo/audio debug-test dispatch, WiFi drops,
Resonant Grid touch) has a confirmed fix above. Add new entries here the moment a fresh
symptom shows up but isn't understood yet, so the next session picks up where this one left
off instead of re-discovering the same ground.
