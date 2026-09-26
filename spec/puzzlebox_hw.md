# Chronolab Puzzlebox — Hardware Guide

Practical reference for every piece of hardware in this project: what it is, how it is
wired, how the firmware drives it, and how you must use it.

Everything here is derived from the firmware in this repo. The authoritative sources are:

- [firmware/main/src/devices/device_pins.h](firmware/main/src/devices/device_pins.h) — main controller GPIO map
- [firmware/main/src/devices/encoder_hardware_config.h](firmware/main/src/devices/encoder_hardware_config.h) — I2C expander / LED driver map
- [boards/esp32-8048S050C.json](boards/esp32-8048S050C.json) and [boards/esp32-2424S012N.json](boards/esp32-2424S012N.json) — display board pin maps
- [firmware/shared/include/config/network_config.h](firmware/shared/include/config/network_config.h) — network identities

---

## 1. System Overview

Four independent MCU nodes. The main controller owns all game logic and all physical
puzzle I/O; the other three are satellites.

| Node | Board | PlatformIO env | Role | Link to main |
|---|---|---|---|---|
| Main controller | ESP32 DevKit V1 (`esp32dev`) | `main_controller` | Game logic, all sensors/actuators, Wi-Fi AP, operator web UI | — (is the hub) |
| Sidecar | ABRobot ESP32-C3 0.42" OLED | `c3_sidecar` | Status beacon: onboard OLED + LED | ESP-NOW + Wi-Fi STA |
| Main display | Sunton ESP32-8048S050C (ESP32-S3, 800×480 IPS, touch) | `s3_display` | Player-facing UI, Tetris rendering, audio cues | Wi-Fi STA → HTTP/JSON |
| Round display | Sunton ESP32-2424S012N (ESP32-C3, 240×240 round) | `round_display_2424` | Secondary status display | Wi-Fi STA → HTTP/JSON |

```
                    Wi-Fi AP "Chronolab-X13" ch.6 / 192.168.4.1
   +--------------------------+---------------------------+
   |                                                      |
+--+-------------------+   ESP-NOW ch.6   +---------------+--+
|  MAIN CONTROLLER     |<---------------->|  C3 SIDECAR      |
|  ESP32 DevKit V1     |                  |  .210            |
|                      |   HTTP/JSON      +------------------+
|  |- SPI  -> RC522    |<---------------->|  S3 DISPLAY .211 |
|  |- I2C  -> RTC      |                  +------------------+
|  |         MCP23017  |<---------------->|  C3 ROUND  .212  |
|  |         PCA9685   |                  +------------------+
|  |- 3-wire -> TM1638 |
|  |- 1-wire -> WS2812 |
|  +- GPIO  -> servo, vibration, hidden sensors, colour sensor
+----------------------+
```

**Rule:** only the main controller touches puzzle hardware. Display and sidecar are
output/echo devices. If you add a sensor, it goes on the main controller unless you
have a very good reason.

---

## 2. Main Controller Pin Map

From [device_pins.h](firmware/main/src/devices/device_pins.h). ESP32 DevKit V1.

| GPIO | Function | Direction | Notes |
|---|---|---|---|
| 0 | TCS3200 `S2` | OUT | ⚠️ **Strap pin** — must be HIGH at boot. Do not let the module pull it low. |
| 2 | TCS3200 `S3` | OUT | ⚠️ **Strap pin** — onboard LED on many DevKits. |
| 4 | TM1638 `STB` | OUT | |
| 5 | TM1638 `CLK` | OUT | ⚠️ Strap pin (must be HIGH at boot). |
| 13 | WS2812 data | OUT | Both 8×8 matrices, daisy-chained |
| 14 | Hidden sensor 2 | IN_PULLUP | Active LOW |
| 15 | Vibration module `IN` | OUT | Active HIGH |
| 16 | RC522 `SS` | OUT | |
| 17 | RC522 `RST` | OUT | |
| 18 | RC522 `SCK` | OUT | VSPI |
| 19 | RC522 `MISO` | IN | VSPI |
| 21 | I2C `SDA` | bidir | RTC + MCP23017 + PCA9685 |
| 22 | I2C `SCL` | OUT | RTC + MCP23017 + PCA9685 |
| 23 | RC522 `MOSI` | OUT | VSPI |
| 25 | Servo signal | OUT | LEDC PWM 50 Hz |
| 26 | TM1638 `DIO` | bidir | Switched IN/OUT in software |
| 34 | TCS3200 `OUT` | IN | **Input-only pin**, no internal pull-up |
| 35 | Hidden sensor 1 | IN | **Input-only pin**, no internal pull-up → external pull-up required |

**Free / reserved:** GPIO 27, 32, 33 are declared in `device_pins.h` as
`kEncoderSwitch`, `kEncoderA`, `kEncoderB` but are **dead constants** — the three rotary
encoders moved to the MCP23017. Those pins are physically available. Nothing reads those
constants today.

**Do not use:** GPIO 6–11 (SPI flash). GPIO 36/39 are free but input-only and currently unused.

### Power rules

- RC522, RTC, MCP23017, PCA9685, TM1638: **3.3 V**. Never 5 V on the RC522.
- WS2812 matrices: **5 V**, own rail, common GND, buffer cap near the first matrix.
- Servo: **5 V**, own rail with buffer cap, common GND. Never from the 3.3 V regulator.
- Vibration module: per its own silkscreen (usually 5 V). It is a driver board — the
  ESP32 only drives `IN`, never the motor directly.
- Every subsystem must share GND with the ESP32. Missing common GND is the #1 failure.

---

## 3. Bus Inventory

### 3.1 VSPI — RC522 only

`SPI.begin(18, 19, 23, 16)` in [rfid_service.cpp](firmware/main/src/devices/rfid_service.cpp).
The bus is not shared with anything else on the main controller. If you add an SPI
device, give it its own CS and be aware the RC522 driver assumes exclusive timing.

### 3.2 I2C — three devices on one bus

`Wire.begin(21, 22)` is called twice: once explicitly in
[`AppController::begin()`](firmware/main/src/app/app_controller.cpp) and once inside
`RtcService::begin()`. Harmless, but it means **the I2C bus is initialised before any
device driver runs** — you can rely on that.

| Address | Device | Owner |
|---|---|---|
| `0x68` | DS3231 or DS1307 RTC | `RtcService` |
| `0x20` | MCP23017 GPIO expander | `InputPanelService` |
| `0x40` | PCA9685 16-ch PWM driver | `InputPanelService` |

Pull-ups: most breakout modules carry their own 4.7 kΩ. Three modules in parallel gives
~1.5 kΩ, still fine at 100 kHz. If you add more I2C devices, start removing module pull-ups.

⚠️ **`LedDriverService` is a second, conflicting PCA9685 driver.** It exists in
[led_driver_service.cpp](firmware/main/src/devices/led_driver_service.cpp) but is *not*
instantiated by `AppController`. It uses a different channel layout (3 channels per
encoder, `index*3`) and a different PWM frequency (200 Hz vs 1600 Hz). **Do not enable it**
alongside `InputPanelService` — they would fight over the same chip. Treat
`InputPanelService` as the single owner of the PCA9685.

### 3.3 TM1638 3-wire — bit-banged

`STB`/`CLK`/`DIO` on GPIO 4/5/26. `DIO` is bidirectional: the driver flips it to
`INPUT_PULLUP` to read buttons and back to `OUTPUT` afterwards. Never wire the module so
5 V can come back down `DIO` into the ESP32.

### 3.4 WS2812 1-wire

GPIO 13, 128 pixels total (2 × 8×8 chained), 800 kHz, GRB order.

---

## 4. Devices on the Main Controller

Every device is a service class in `firmware/main/src/devices/`, owned by
`AppController`. All follow the same contract: `begin()` once in `AppController::begin()`,
`poll()`/`tick()` every loop.

---

### 4.1 WS2812 LED Matrices — `MatrixService`

**Hardware:** 2 × WS2812B 8×8, chained. Matrix 1 `DIN` ← GPIO13, matrix 1 `DOUT` →
matrix 2 `DIN`. Both on 5 V + common GND.

**Firmware:** [matrix_service.h/.cpp](firmware/main/src/devices/matrix_service.h) —
`Adafruit_NeoPixel strip_{128, 13, NEO_GRB + NEO_KHZ800}`. The pin `13` is hardcoded in
the constructor, *not* read from `pins::kMatrixData`.

**Brightness is fixed at 24/255** (`setBrightness(24)`). This is a deliberate current
limit — 128 pixels at full white would draw ~7.7 A. Raise it only if your 5 V rail can
take it.

**How to use:**

```cpp
matrixService_.renderGameState(protocol::GameState::Active);  // colour per game state
matrixService_.runTest();                                     // alternating test pattern
```

The panel is split in halves (pixels 0–63 = left, 64–127 = right), each half gets its own
colour. Colours per state are hardcoded in `renderGameState()`. It is called automatically
from `AppController::updateOutputsForState()` on every state transition — you normally
never call it yourself.

**Operator test:** `POST /api/test/matrix`.

---

### 4.2 RC522 RFID Reader — `RfidService`

**Hardware:** MFRC522 breakout on **3.3 V**. `SS`→16, `RST`→17, `SCK`→18, `MISO`→19,
`MOSI`→23.

**Firmware:** [rfid_service.h/.cpp](firmware/main/src/devices/rfid_service.h). Pins `16, 17`
are hardcoded in the `MFRC522 reader_{16, 17}` member initialiser.

**Presence detection:** `begin()` reads `VersionReg`; if it returns `0x00` or `0xFF` the
service marks itself not-ready and `poll()` becomes a no-op. A missing or miswired reader
degrades silently instead of hanging.

**How to use:**

```cpp
rfidService_.poll();                      // every loop
String tag  = rfidService_.takeEventTag(); // one-shot: non-empty only on a NEW scan
String last = rfidService_.lastSeenTag();  // sticky: last UID, or "geen scan"
```

`takeEventTag()` consumes the event — call it exactly once per loop, which
`syncDiagnostics()` already does. UIDs are formatted as uppercase hex without separators
(e.g. `A1B2C3D4`).

**Game role:** puzzle `Rfid` ("RFID toegang"). The expected tag is a label
(`"Tag A"`/`"Tag B"`/`"Tag C"`) chosen per scenario in `runtime_.correctRfidTag`.

---

### 4.3 RTC — `RtcService`

**Hardware:** DS3231 (preferred) or DS1307 on I2C, address `0x68`. Battery-backed.

**Firmware:** [rtc_service.h/.cpp](firmware/main/src/devices/rtc_service.h). `begin()`
tries DS3231 first, falls back to DS1307, and marks itself not-ready if neither answers.

**How to use:**

```cpp
rtcService_.poll();                      // refreshes at most once per second
bool ok  = rtcService_.isReady();
String t = rtcService_.formattedNow();   // "YYYY-MM-DD HH:MM" or "niet beschikbaar"
```

The service is **read-only** — it never sets the RTC. Set the time out-of-band (a separate
sketch, or by adding an `adjust()` call) before you rely on it for a puzzle.

**Game role:** puzzle `Rtc` ("Tijdreactor"). **Operator test:** `POST /api/test/read-rtc`.

---

### 4.4 TM1638 LED&KEY Module — `InputPanelService` (display half)

**Hardware:** TM1638 board with 8× 7-segment digits, 8 red LEDs, 8 buttons (S1–S8).
`STB`→4, `CLK`→5, `DIO`→26, on 3.3 V.

**Firmware:** [input_panel_service.h/.cpp](firmware/main/src/devices/input_panel_service.h).
Fully bit-banged, no library.

**Button reading is clone-tolerant.** `decodeTm1638ButtonPair()` accepts the two common
bit layouts (bits 0/4 and bits 1/5), so S5–S8 work on both genuine and clone boards. If
your buttons come back wrong, that function is the place to look.

**Brightness** is 1 (idle) or 7 (active), driven by `setStatusLed(bool)`, which
`updateOutputsForState()` sets true during `Briefing` and `Active`.

**Display modes** — `InputPanelService::Mode`. Only one owner at a time:

| Mode | Digits 0–3 | Digits 4–7 | LEDs |
|---|---|---|---|
| `kStatus` | `MM.SS` remaining | state code (`BOOT`/`IDLE`/`ACTV`/`SUCC`/…), dot on digit 7 = sidecar online | mirror button state |
| `kPatternPuzzle` | phase (`PATT`/`SHOW`/`INPT`/`ERR`/`GOOD`) | `progress-target` | pattern step, blinking |
| `kTetrisPuzzle` | phase (`PLAY`/`PAUS`/`GOAL`) | `level-target` | control hint mask `0x3F` |

**How to use:**

```cpp
inputPanelService_.renderStatus(gameState, remainingSeconds, sidecarOnline);
inputPanelService_.renderPatternPuzzle(phase, ledMask, progress, targetLength, blinkOn);
inputPanelService_.renderTetrisPuzzle(phase, level, targetLevel, ledMask, blinkOn);
inputPanelService_.setStatusLed(true);   // brightness boost
```

All three render calls are **idempotent and change-gated** — they early-return if nothing
changed, so it is safe (and intended) to call them every loop. `renderStatus()` is
suppressed by `AppController::tick()` while a puzzle owns the panel.

**Reading buttons:**

```cpp
bool any = inputPanelService_.ledKeyPressed();      // live state
std::uint8_t mask = inputPanelService_.ledKeyButtonMask();

std::uint8_t buttonIndex;
while (inputPanelService_.takeLedKeyButtonPress(buttonIndex)) {
  // consumes one queued press, lowest index first
}
```

**Tetris control mapping** (`handleTetrisButton`):

| Button | Action |
|---|---|
| S1 (0) | move left |
| S2 (1) | rotate |
| S3 (2) | move right |
| S4 (3) | soft drop (+1 pt, locks on collision) |
| S5 (4) | hard drop (+2 pts per row, then locks) |
| S6 (5) | pause / resume (1200 ms guard after phase start) |
| S7 (6) | unused |
| S8 (7) | unused |

---

### 4.5 Three Rotary Encoders — `InputPanelService` (encoder half)

The encoders do **not** hang off the ESP32 directly. Inputs go through an MCP23017, LEDs
through a PCA9685. Both on the shared I2C bus.

#### Wiring — MCP23017 @ `0x20` (inputs)

| Expander pin | Signal | Mode | Active level |
|---|---|---|---|
| GPA0 / GPA1 | Encoder 1 `A` / `B` | `INPUT_PULLUP` | — |
| GPA2 | Encoder 1 `SW` | `INPUT_PULLUP` | **LOW** (switch to GND) |
| GPA3 / GPA4 | Encoder 2 `A` / `B` | `INPUT_PULLUP` | — |
| GPA5 | Encoder 2 `SW` | `INPUT_PULLUP` | **LOW** (switch to GND) |
| GPA6 / GPA7 | Encoder 3 `A` / `B` | `INPUT_PULLUP` | — |
| GPB0 | Encoder 3 `SW` | `INPUT` (no pull-up) | **HIGH** — switch to 3.3 V, external 10 kΩ to GND |

⚠️ **Encoder 3's button is wired inversely to the other two.** It needs an *external*
10 kΩ pull-down and switches to 3.3 V. This asymmetry is baked into `pollEncoders()`:

```cpp
encoders_[index].buttonPressed = (index == 2) ? (swState == HIGH) : (swState == LOW);
```

If you rewire encoder 3 to match encoders 1 and 2, you must change that line.

#### Wiring — PCA9685 @ `0x40` (LEDs)

PWM 1600 Hz, 12-bit (0–4095), oscillator set to 27 MHz. Channel map from
[encoder_hardware_config.h](firmware/main/src/devices/encoder_hardware_config.h):

| Channel | Encoder | Colour | LED type |
|---|---|---|---|
| 0 / 1 | Encoder 1 | R / G | bi-colour, **common cathode** (active high) |
| 2 / 3 | Encoder 2 | R / G | bi-colour, **common cathode** (active high) |
| 4 / 5 / 6 | Encoder 3 | R / G / B | RGB, **common anode** (active low → PWM inverted in software) |
| 7–15 | free | | |

Series resistors, and the brightness calibration derived from them:

| Colour | Resistor | Software multiplier |
|---|---|---|
| Red | 330 Ω | 1.00 (baseline) |
| Green | 150 Ω | 0.45 |
| Blue (encoder 3) | 135 Ω | 0.41 |

The multipliers exist so `(255, 255, 255)` looks white instead of green-cyan. **If you
change a resistor, update `kBicolor*Multiplier` / `kRgb*Multiplier`** — the firmware has
no way to detect it.

Encoders 1 and 2 have **no blue channel**. `setEncoderLed(0, r, g, b)` silently drops `b`
because `getColorChannelCount()` returns 2 for indices 0 and 1.

#### How to use the encoders

```cpp
const auto& all = inputPanelService_.encoders();       // std::array<EncoderState, 3>
const EncoderState& e = inputPanelService_.encoder(1);
int   detents = e.value;          // signed, free-running, never reset by the service
bool  pressed = e.buttonPressed;  // debounced only by poll rate
```

`value` is a raw quadrature accumulator with no detent division and no wrap — track deltas
against your own baseline, don't treat it as an absolute position.

#### How to use the encoder LEDs

```cpp
// Immediate
inputPanelService_.setEncoderLed(0, 255, 0, 0);          // encoder 1 red
inputPanelService_.setEncoderLed(2, EncoderLedState{0, 0, 255});

// Animated  (easing: kLinear, kEaseIn, kEaseOut, kEaseInOut, kBounce)
inputPanelService_.fadeEncoderLed(1, target, 400);
inputPanelService_.pulseEncoderLed(1, target, 600);      // out and back
inputPanelService_.blinkEncoderLed(1, target, 3, 200);   // 3 blinks
inputPanelService_.queueEncoderAnimation(1, target, 500, EasingFunction::kBounce, onDone);
inputPanelService_.clearEncoderAnimation(1);
```

Animation rules you must respect:

- **4 slots per encoder.** A 5th queued animation overwrites slot 4 (with a serial warning).
- **Only the first active slot advances per tick** — it is a queue, not a mixer.
- `blinkEncoderLed(n, …)` queues `2n` animations via chained callbacks; `count > 2`
  overflows the 4-slot queue. Keep blink counts small or drive it yourself.
- Animations advance inside `poll()`. If your code stops calling `poll()`, LEDs freeze
  mid-fade.

**Remote control:** `POST /api/encoder-leds` with `{"encoder":0,"r":255,"g":0,"b":0}`.
Index must be 0–2 or you get `400 invalid_encoder_index`.

#### Boot self-test

`InputPanelService::begin()` runs a **blocking 2-second LED test**: encoder 1 red,
encoder 2 green, encoder 3 blue for 1500 ms, then all off. This is intentional wiring
verification. It costs 2 s of boot time — if you need faster boot, that block is the first
thing to cut.

---

### 4.6 Hidden Sensors — `HiddenTriggerService`

**Hardware:** two reed switches, hall sensors, or plain contacts.

| Sensor | GPIO | Mode | Wiring |
|---|---|---|---|
| 1 | 35 | `INPUT` | **Input-only pin, no internal pull-up — you MUST fit an external 10 kΩ pull-up to 3.3 V.** Switch to GND. |
| 2 | 14 | `INPUT_PULLUP` | Switch straight to GND, no external parts. |

Both are **active LOW**. This asymmetry is the most common bring-up mistake: sensor 1
without its external pull-up floats and reports random triggers.

**How to use:**

```cpp
hiddenTriggerService_.poll();                     // every loop
bool s1 = hiddenTriggerService_.sensor1Active();
bool s2 = hiddenTriggerService_.sensor2Active();
```

There is **no debouncing**. For a mechanical reed switch used as a puzzle trigger that is
fine; if you need an edge count, debounce in your own code.

**Game role:** puzzle `Hidden` ("Verborgen trigger"), with the correct sensor picked per
scenario in `runtime_.activeHiddenTrigger` (`"Sensor 1"` / `"Sensor 2"`).

---

### 4.7 TCS3200/TCS230 Colour Sensor — `ColorSensorService`

**Hardware wiring — this module MUST be strapped, not fully wired:**

| Module pin | Connect to | Why |
|---|---|---|
| `OUT` | GPIO34 | frequency output, 3.3 V logic only |
| `S2` | GPIO0 | filter select |
| `S3` | GPIO2 | filter select |
| `S0` | **3.3 V (HIGH)** | with S1 low = 20 % output scaling |
| `S1` | **GND (LOW)** | " |
| `OE` | **GND** | permanently enabled |
| `VCC` / `GND` | 3.3 V / common GND | |

Hard-strapping `S0`, `S1` and `OE` saves three GPIOs. It also means **you cannot change
the scaling at runtime** — 20 % is fixed.

⚠️ **GPIO0 and GPIO2 are ESP32 strap pins.** GPIO0 must be HIGH at boot for normal
startup; GPIO2 must not be held high while GPIO0 is low. A colour-sensor module that loads
these pins can prevent the board from booting or from entering download mode. If flashing
suddenly fails, unplug the colour sensor first. GPIO34 is input-only, which is correct
here — `OUT` is a pure input.

**Measurement**, from [color_sensor_service.cpp](firmware/main/src/devices/color_sensor_service.cpp):

- Samples every **250 ms**, all three filters per cycle
- 2 samples per filter, 300 µs settle after each filter change
- `pulseIn()` with an **8 ms timeout** → worst case ~96 ms blocked inside `poll()` when
  nothing is in front of the sensor. This is the single most expensive call in the main
  loop; keep it in mind if you add timing-sensitive code.
- Signal floor: 40 Hz. Below that → `NoSignal`.

**Classification** (`classifyColor()`) is ratio-based, not calibrated:

| Result | Rule |
|---|---|
| `White` | max ≤ min + max/5 (all three channels close) |
| `Yellow` | R > 1.3×B **and** G > 1.2×B **and** \|R−G\| ≤ max(R,G)/3 |
| `Red` / `Green` / `Blue` | that channel > 1.2× **both** others |
| `Unknown` | signal present, no rule matched |
| `NoSignal` | max < 40 Hz |

**How to use:**

```cpp
colorSensorService_.poll();                                 // every loop, self-rate-limited
bool present   = colorSensorService_.hasSignal();
auto colour    = colorSensorService_.detectedColor();       // enum
const char* nl = colorSensorService_.detectedColorLabel();  // "rood"/"groen"/"blauw"/"geel"/"wit"/…
std::uint32_t r = colorSensorService_.redFrequencyHz();     // also green/blue, for tuning
```

Ambient light changes the absolute frequencies but the classifier is ratio-based, so it
mostly survives. Mount the sensor close to the target and shield it. Use the raw Hz getters
(exposed in `/api/diagnostics`) to tune before trusting the classifier.

**Not yet wired to a puzzle** — it is reported in diagnostics only.

---

### 4.8 Servo Lock — `ServoService`

**Hardware:** MG946R or similar. Signal → GPIO25. **Power from a separate 5 V rail** with
a buffer electrolytic close to the servo. Common GND with the ESP32.

⚠️ The [bring-up checklist](docs/bringup-checklist.md) still says GPIO27 for the servo.
**That is stale — the firmware uses GPIO25.**

**Firmware:** [servo_service.h/.cpp](firmware/main/src/devices/servo_service.h), ESP32Servo,
50 Hz, pulse range 500–2400 µs.

| Position | Angle |
|---|---|
| Closed | 12° |
| Open | 96° |

Tune those two constants (`kServoClosedDegrees` / `kServoOpenDegrees`) to your actual latch
geometry. Wrong values stall the servo against the mechanism and burn current.

**How to use:**

```cpp
servoService_.open();
servoService_.close();
bool isOpen = servoService_.isOpen();
```

The servo stays **attached and powered at its target angle** — it is never detached, so it
holds torque continuously. If it buzzes at rest, that is the holding current; the fix is
mechanical (relieve the load), or add a `detach()` after each move.

`updateOutputsForState()` drives it automatically: **open on `Success`**, closed on every
state except `Maintenance` (left untouched so you can service the lid).

**Operator tests:** `POST /api/test/servo-open`, `/api/test/servo-close`, and
`/api/game/emergency-open` (opens regardless of game state).

---

### 4.9 Vibration Motor — `VibrationService`

**Hardware:** OPEN-SMART vibration module — a **driver board**, not a bare motor.
`IN` → GPIO15, `VCC` per the module's own marking, `GND` shared. Never drive a bare motor
from a GPIO, and never let module `VCC` reach a signal pin.

**Firmware:** [vibration_service.h/.cpp](firmware/main/src/devices/vibration_service.h).
Active HIGH, non-blocking one-shot pulses.

**How to use:**

```cpp
vibrationService_.tick();          // every loop — this is what ends the pulse
vibrationService_.pulse(200);      // ms; a new pulse restarts the timer
vibrationService_.stop();
bool on = vibrationService_.isActive();
```

⚠️ **If you stop calling `tick()`, the motor never stops.** The pulse end is polled, not
interrupt-driven.

Built-in game feedback:

| Event | Duration |
|---|---|
| Run start | 120 ms |
| Pattern-puzzle mistake | 220 ms |
| Final success | 420 ms |
| Last-minute countdown warning | variable, per `lastMinuteWarningPulseDuration()` |

**Operator tests:** `POST /api/test/vibration-pulse` (350 ms), `/api/test/vibration-stop`.

---

## 5. C3 Sidecar Node

**Board:** ABRobot ESP32-C3 with onboard 0.42" OLED. No external parts needed.

| Resource | Pin | Notes |
|---|---|---|
| Onboard LED | GPIO8 | **Active LOW** |
| OLED `SDA` | GPIO5 | SSD1306 72×40, I2C addr `0x3C` |
| OLED `SCL` | GPIO6 | |

**Firmware:** [sidecar_controller.cpp](firmware/sidecar/src/app/sidecar_controller.cpp),
U8g2 `U8G2_SSD1306_72X40_ER_F_HW_I2C`, font `u8g2_font_4x6_tr`, 6 lines at 6 px pitch.

**LED blink code — read the box's state from across the room:**

| Interval | Meaning |
|---|---|
| 180 ms (fast) | no controller found |
| 250 ms | scene `Active` |
| 120 ms (fastest) | scene `Timeout` |
| 500 ms | scene `Paused` |
| 700 ms | idle / briefing / maintenance |
| solid on | scene `Success` |

**OLED layout** (redrawn every 250 ms):

```
CTRL:OK        controller link
SCN :ACT       scene
TMR :12:34     remaining time
IP  :192...    alternates with VER: every 4 s
OTA :READY     or  OTA :  42%  /  OTA :ERR n
PULL:NONE      pull-update state
```

The sidecar is **output only** — it has no inputs and cannot influence the game. It is a
status beacon and an ESP-NOW link test.

---

## 6. Display Nodes

Both display nodes are **Sunton smart-display boards**. Their pin maps live entirely in the
board JSON files; app code reads them through `DisplayBoardProfile` and never hardcodes a
pin. Both run the same firmware source (`firmware/display/`), branched at compile time on
`ESP32_2424S012N` / `ESP32_8048S050C`.

### 6.1 Main Display — ESP32-8048S050C

ESP32-S3, 16 MB flash, PSRAM, 800×480 IPS, capacitive touch, I2S audio, TF slot.

| Subsystem | Pins |
|---|---|
| RGB sync | `HSYNC` 39, `VSYNC` 41, `DE` 40, `PCLK` 42 @ 14 MHz |
| RGB data R0–R4 | 8, 3, 46, 9, 1 |
| RGB data G0–G5 | 5, 6, 7, 15, 16, 4 |
| RGB data B0–B4 | 45, 48, 47, 21, 14 |
| Backlight | GPIO2, duty **0.68** |
| Touch (GT911, I2C) | `SDA` 19, `SCL` 20, `RST` 38, `INT` not connected, addr `0x5D`, 400 kHz |
| Audio (I2S) | `BCK` 0, `LRCK` 18, `DIN` 17 |
| TF card (SPI) | `CS` 10, `MOSI` 11, `SCLK` 12, `MISO` 13 |

The RGB panel consumes 21 GPIOs — this board has essentially **no free pins**. Do not plan
on adding sensors here.

Framebuffer lives in PSRAM (`FB_IN_PSRAM=true`), LVGL buffer is a full 800×480.
`SET_LOOP_TASK_STACK_SIZE(16 * 1024)` in [display/src/main.cpp](firmware/display/src/main.cpp)
is a **required crash fix — do not remove it.**

### 6.2 Round Display — ESP32-2424S012N

ESP32-C3, 4 MB flash, 240×240 round GC9A01 over SPI. **No touch, no audio, no TF.**

| Subsystem | Pins |
|---|---|
| SPI `MOSI` | GPIO7 |
| SPI `SCLK` | GPIO6 |
| SPI `CS` | GPIO10 |
| SPI `DC` | GPIO2 |
| SPI `MISO` / `RESET` | not connected |
| Backlight | GPIO3, duty **1.0** |

80 MHz SPI, `MIRROR_X=true`, BGR colour space. Uses the `min_spiffs.csv` partition table so
the OTA app slots fit in 4 MB — **do not switch it back to the default partitions.**

### 6.3 Display Audio — `DisplaySpeakerService`

Two backends, selected at compile time, in this priority order:

1. **Pin/`tone()` backend** — requires `BOARD_HAS_SPEAK` + `SPEAK`. Neither board defines
   these today, so this path is currently dead.
2. **I2S backend** — requires `BOARD_HAS_AUDIO` + `I2S_BCK`/`I2S_LRCK`/`I2S_DIN`.
   **Active on the 8048S050C only.** 16 kHz, 16-bit, Philips mode, rendered by a dedicated
   FreeRTOS task (`displayAudio`, 4 KB stack).
3. **Neither** → `ready_ = false`, cues are logged but silent. That is the round display's
   situation, and it is fine.

**There is no speaker on the main controller.** Physical speaker wiring there was removed.
The main controller only *emits cue IDs* (`protocol::AudioCueId`) into diagnostics; the
display decides whether it can play them.

Cues and durations, from [audio_cue.h](firmware/shared/include/protocol/audio_cue.h):
`ShortBeep` 320 ms, `DoubleBeep` 340 ms, `TestMelody` 1240 ms, `Hint` 440 ms,
`Countdown` 390 ms, `RunStart` 380 ms, `Error` 740 ms, `Success` 420 ms, `Endgame` 980 ms.

Tones are defined musically (root note index + tick length + semitone offsets), so you
retune a whole phrase by changing `rootNoteIndex`/`tickMs` rather than rewriting
frequencies. Waveforms: square or triangle.

**Operator tests:** `POST /api/test/speaker-beep`, `/api/test/speaker-melody`,
`/api/test/speaker-stop`.

---

## 7. Networking

### 7.1 Identities

| Node | IP | Hostname | Role |
|---|---|---|---|
| Main controller | `192.168.4.1` | `chronolab-main` | **AP** (`WIFI_AP_STA`) |
| Sidecar | `192.168.4.210` | `chronolab-sidecar` | STA, static |
| Main display | `192.168.4.211` | `chronolab-display-main` | STA, static |
| Round display | `192.168.4.212` | `chronolab-display-round` | STA, static |

- SSID `Chronolab-X13`, password `chronolab13`, **channel 6**
- OTA password `chronolab-ota`, port `3232`

⚠️ **The Wi-Fi channel and the ESP-NOW channel are the same constant**
(`config::kAccessPointChannel`). Changing the AP channel moves ESP-NOW with it — which is
required, since an ESP32 has one radio and ESP-NOW peers must sit on the AP's channel.
Never hardcode a different channel in one place only.

All static IPs are unique by design. Reusing `192.168.4.2` for the sidecar (an old value)
will break it.

### 7.2 ESP-NOW — main ↔ sidecar

Custom packed protocol in
[espnow_protocol.h](firmware/shared/include/protocol/espnow_protocol.h): a 12-byte
`PacketEnvelope` plus up to 32 bytes of payload, unencrypted.

Handshake: the sidecar broadcasts `Hello` to `FF:FF:FF:FF:FF:FF` every 1500 ms until the
main controller replies `HelloAck` and registers it as a peer. Heartbeats every 1000 ms;
the sidecar declares the controller lost after **10 s** of silence.

Message types: `Hello`, `HelloAck`, `Heartbeat`, `StateSnapshot`, `SceneSet`, `TimerSet`,
`PuzzleSet`, `HintLevelSet`, `DisplayTextShort`, `NodeTest`, `ResetSideEffects`,
`LocalInputEvent`, `LocalSensorEvent`, `NodeFault`, `DiagStatus`, `Ack`.

**Payloads are hard-capped at 32 bytes.** Adding a field to a payload struct is a breaking
wire change — bump `kProtocolVersion` if you do.

### 7.3 HTTP / WebSocket — main ↔ displays and operator

Main controller serves on **port 80** (HTTP) and **port 81** (WebSocket snapshot push).

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | operator page |
| GET | `/update` | firmware upload page |
| GET | `/api/status` | full snapshot |
| GET | `/api/config` | setup |
| GET | `/api/diagnostics` | all hardware readings |
| GET | `/api/tetris`, `/api/tetris/input` | board state / queued inputs |
| POST | `/api/config` | update setup |
| POST | `/api/encoder-leds` | `{"encoder":0-2,"r":0-255,"g":…,"b":…}` |
| POST | `/api/game/{start,pause,resume,reset,emergency-open}` | game control |
| POST | `/api/hints/{1,2}` | hints |
| POST | `/api/time/{add-minute,remove-minute}` | timer nudge |
| POST | `/api/puzzle/{skip-current,solve-current}` | puzzle override |
| POST | `/api/test/matrix` | matrix pattern |
| POST | `/api/test/{servo-open,servo-close}` | servo |
| POST | `/api/test/{vibration-pulse,vibration-stop}` | vibration |
| POST | `/api/test/{speaker-beep,speaker-melody,speaker-stop}` | display audio cue |
| POST | `/api/test/read-rtc` | RTC refresh |
| POST | `/api/debug/tetris/start` | start Tetris standalone |
| POST | `/api/storage/clear-history` | wipe high scores |
| POST | `/api/settings/reload` | reload settings |

Display polling intervals (validated — do not tighten without testing; the display used to
flap between online and offline):

| Situation | Interval |
|---|---|
| Online full snapshot | 750 ms |
| Debug full snapshot | 1500 ms |
| Debug diagnostics only | 250 ms |
| Tetris board | 20 ms |
| Follow-up after an action | 75 ms |
| Offline retry | 1500 ms |
| HTTP connect / total timeout | 300 ms / 800 ms |

---

## 8. Puzzle → Hardware Map

Six puzzles, played in order (`resetPuzzleStatuses()`):

| # | `PuzzleId` | Label | Hardware |
|---|---|---|---|
| 1 | `Pattern` | Energiepatroon | TM1638 LEDs + buttons (Simon-style playback/repeat) |
| 2 | `Rfid` | RFID toegang | RC522 + tags |
| 3 | `Rtc` | Tijdreactor | RTC module |
| 4 | `Hidden` | Verborgen trigger | Hidden sensor 1 or 2 |
| 5 | `Tetris` | Tetris reactor | TM1638 buttons (input) + display (rendering) |
| 6 | `FinalLock` | Final lock | Servo (opens on success) |

Pattern-puzzle sequence length by difficulty: `Short` 3, `Normal` 4, `Long` 6. Playback
timing by global difficulty: Easy 780/340 ms, Medium 520/220 ms, Hard 320/140 ms with a
15 s error penalty.

Tetris board is 10 × 16. `kDisplayOwnsTetris = true` — the display renders and reports
progress back via `POST /api/tetris/progress`; the main controller owns the input queue.

The colour sensor and the encoders are **not yet bound to a puzzle**. They are live in
diagnostics and ready to be used. The parked design for an encoder puzzle is in
[docs/rgb-rotary-encoder-plan.md](docs/rgb-rotary-encoder-plan.md): turn a channel into a
target zone, colour feedback for warm/cold/stable, press to confirm.

---

## 9. Adding Hardware — Working Rules

1. **Breadboard first.** Never add a module straight into the box.
2. **One module at a time.** Verify in `/api/diagnostics` before adding the next.
3. **Order matters.** Matrices → RC522 → RTC → TM1638 → encoders → hidden sensors →
   colour sensor → vibration → **servo last** (it is the noisiest load).
4. **Common GND, always.** Then check it again.
5. **New device = new service class** in `firmware/main/src/devices/`, following the
   `begin()` / `poll()` contract, owned by `AppController`, surfaced in `syncDiagnostics()`.
   Do not put device I/O in `AppController` directly.
6. **Pins go in `device_pins.h`** — and then actually *use* the constant. `MatrixService`
   and `RfidService` currently hardcode their pins in member initialisers; don't copy that
   pattern.
7. **Never block in `poll()`.** The colour sensor's ~96 ms worst case is already the ceiling
   for this loop.
8. **Avoid GPIO 0, 2, 5, 12, 15** for anything that loads the pin at boot. The colour sensor
   already sits on two strap pins — unplug it before flashing if the ESP32 refuses download
   mode.
9. **Input-only pins (34, 35, 36, 39)** have no internal pull-ups. Fit external ones.
10. **Update the resistor calibration** in `encoder_hardware_config.h` if you change any
    encoder LED series resistor.

---

## 10. Bring-Up Sequence

```bash
c:/Python314/python.exe -m platformio run -e main_controller -t upload
```
```bash
c:/Python314/python.exe -m platformio run -e c3_sidecar -t upload
```
```bash
c:/Python314/python.exe -m platformio run -e s3_display -t upload
```
```bash
c:/Python314/python.exe -m platformio run -e round_display_2424 -t upload
```

Helper scripts (add `-Port COMx` as needed): `./flash-devkit.ps1` (COM10),
`./flash-c3.ps1` (COM4), `./flash-display.ps1` (COM11), `./flash-2424.ps1` (COM12).

Serial monitor at **115200**:

```bash
c:/Python314/python.exe -m platformio device monitor -b 115200 -p COM10
```

### What a healthy boot looks like

**Main controller:**
```
[color-sensor] TCS3200/TCS230 on OUT=34 S2=0 S3=2 (verwacht: OE laag, S0 hoog, S1 laag)
[input_panel] MCP23017 ready, 3 encoders configured
[input_panel] PCA9685 configured: freq=1600 Hz
[input_panel] Test: E0=RED, E1=GREEN, E2=BLUE      <- watch the encoders here
[web] AP ready on 192.168.4.1
[main] Chronolab main controller ready
```

**Sidecar:**
```
[sidecar] C3 sidecar ready for ESP-NOW sync
[sidecar] scene=... connected=yes ...
```

**Round display:**
```
[display] Chronolab display node ready
board=ESP32-2424S012N | GC9A01 SPI | 240x240 | round
pins=BCKL 3 | SPI MOSI7 MISO- SCLK6 CS10 DC2 RST-
```

### First checks

1. Join Wi-Fi `Chronolab-X13` / `chronolab13`
2. `http://192.168.4.1/api/status` returns JSON
3. `http://192.168.4.1/api/diagnostics` — walk every field: `rtcOk`, `rfidLastSeen`,
   `encoderValues`, `sensor1`/`sensor2`, `colorDetected`, `servoOpen`
4. Watch the boot self-test (encoder 1 red, encoder 2 green, encoder 3 blue) or drive
   `POST /api/encoder-leds` per channel to verify all nine LED channels
5. Run the servo and vibration test endpoints last

### Recovery

- DevKit won't flash: `./enter-devkit-download-mode.ps1 -Port COM10`, else hold `BOOT` and
  tap `EN/RESET`. If it still fails, **unplug the colour sensor** (GPIO0/GPIO2).
- OTA: `./ota-devkit.ps1`, `./ota-c3.ps1`, or the browser pages at
  `http://192.168.4.1/update` and `http://192.168.4.210/update`.
- Build a versioned release: `./export-firmware.ps1 -Build -NewVersion` → `./release/`.

---

## 11. Known Discrepancies

Things that are wrong or stale elsewhere in the repo. This document reflects the firmware
as it actually is.

| Where | Says | Reality |
|---|---|---|
| `docs/bringup-checklist.md` | servo on GPIO27 | **GPIO25** (`device_pins.h`) |
| `docs/bringup-checklist.md` | encoder SW on GPIO34 with 10 k pull-up | encoders moved to MCP23017; GPIO34 is the **colour sensor `OUT`** |
| `docs/bringup-checklist.md` | one rotary encoder on GPIO32/33 | **three** encoders on MCP23017 GPA0–GPA7 + GPB0 |
| `device_pins.h` | `kEncoderA/B/Switch` (32/33/27) | dead constants, nothing reads them |
| `led_driver_service.cpp` | second PCA9685 driver | not instantiated; conflicts with `InputPanelService` — do not enable |
| `matrix_service.h` | — | pin `13` hardcoded in the constructor, not from `pins::kMatrixData` |
| `rfid_service.h` | — | pins `16, 17` hardcoded in the constructor |
