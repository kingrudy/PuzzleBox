# Chronolab Puzzlebox — Component Inventory

Single source of truth for **what hardware physically exists in the box right now**.
[puzzlebox_hw.md](puzzlebox_hw.md) describes how everything is wired and driven;
[Specifications.md](Specifications.md) lists confirmed per-unit quirks (wrong register
offsets, swapped channels, etc.) found by testing the actual hardware — check there before
re-debugging something that "should" work per datasheet. This file tracks build state —
connected, not yet connected, or only planned.

**Update this table as you build.** When you add or remove a module, flip its `Status`
here in the same commit. When asking for firmware changes, say which row changed —
that's enough context to know whether code needs a presence guard.

## How this is used

Several services already degrade gracefully when their hardware doesn't answer
(`RfidService`, `RtcService` both have an `isReady()` that goes false on a missing
device). That pattern should extend to every row below marked `Not connected` or
`Planned`:

- New code must not assume a `Connected` status — check this file before writing code
  that talks to a component, and if it's not `Connected`, the code path must degrade
  the same way `RfidService`/`RtcService` do (skip silently, log once, never block
  `poll()`/`loop()`).
- A row flipped from `Not connected` to `Connected` is a signal to go verify the
  corresponding service actually initializes and shows up in `/api/diagnostics`,
  per the bring-up rules in [puzzlebox_hw.md §9](puzzlebox_hw.md#9-adding-hardware--working-rules).
- This file is documentation only — the firmware does not read it. Status here can
  legitimately be ahead or behind of what a given `git` checkout's firmware actually
  handles; treat mismatches as a code TODO, not a doc error.

**Status values:**

| Status | Meaning |
|---|---|
| `Connected` | Physically wired into the box today. Firmware should treat it as present. |
| `Not connected` | Firmware/service code exists but the hardware isn't in the box (yet, or anymore). Code must tolerate its absence. |
| `Planned` | Neither wired nor coded. Idea stage only ([ideas.md](../ideas.md)). |
| `Removed` | Was connected, deliberately taken out. Kept as a row so nobody re-adds dead code assuming it's live. |

---

## Main Controller (ESP32 DevKit V1 — hub)

| Component | Service | Bus / Pins | Status | Notes |
|---|---|---|---|---|
| WS2812 LED matrices (2× 8×8) | `MatrixService` | GPIO13, 1-wire | Removed | |
| RC522 RFID reader | `RfidService` | VSPI: 16/17/18/19/23 | Not connected | Degrades to not-ready if `VersionReg` misreads |
| RTC (DS3231/DS1307) | `RtcService` | I2C `0x68` | Not connected | Falls back DS3231→DS1307→not-ready |
| TM1638 LED&KEY (digits/LEDs/buttons) | `InputPanelService` | 3-wire: 4/5/26 | Connected | |
| Rotary encoder 1 (input) | `InputPanelService` | MCP23017 GPA0/1/2 | Connected | |
| Rotary encoder 1 (RGB LED) | `InputPanelService` | PCA9685 ch0/1 | Connected | Bi-colour, no blue channel |
| Rotary encoder 2 (input) | `InputPanelService` | MCP23017 GPA3/4/5 | Connected | |
| Rotary encoder 2 (RGB LED) | `InputPanelService` | PCA9685 ch2/3 | Connected | Bi-colour, no blue channel |
| Rotary encoder 3 (input) | `InputPanelService` | MCP23017 GPA6/7/GPB0 | Connected | Button wired inverse to 1/2 |
| Rotary encoder 3 (RGB LED) | `InputPanelService` | PCA9685 ch4/5/6 | Connected | Full RGB |
| Hidden sensor 1 | `HiddenTriggerService` | GPIO35 | Not connected | Needs external 10kΩ pull-up |
| Hidden sensor 2 | `HiddenTriggerService` | GPIO14 | Not connected | |
| TCS3200/TCS230 colour sensor | `ColorSensorService` | GPIO34/0/2, hard-strapped | Not connected | Live in diagnostics, not yet bound to a puzzle |
| Servo lock | `ServoService` | GPIO25 | Not connected | |
| Vibration motor (driver board) | `VibrationService` | GPIO15 | Connected | |

## C3 Sidecar Node

| Component | Notes | Status |
|---|---|---|
| ABRobot ESP32-C3 board | ESP-NOW link to main | Not Connected |
| Onboard OLED (SSD1306 72×40) | I2C `0x3C` | Not Connected |
| Onboard LED | GPIO8, active LOW | Not Connected |

## Main Display Node — ESP32-8048S050C

| Component | Notes | Status |
|---|---|---|
| Board itself (800×480 IPS RGB panel) | Wi-Fi STA → HTTP/JSON | Connected |
| Capacitive touch (GT911) | I2C, addr `0x5D` | Connected |
| I2S audio out | `DisplaySpeakerService`, 16kHz | Connected |
| TF/microSD slot | SPI 10/11/12/13 — wired but no firmware use found | Not connected |

## Round Display Node — ESP32-2424S012N

| Component | Notes | Status |
|---|---|---|
| Board itself (240×240 round GC9A01) | Wi-Fi STA → HTTP/JSON | Not Connected |

## Known dead / conflicting code (not a wiring question)

| Item | Notes |
|---|---|
| `LedDriverService` (second PCA9685 driver) | Not instantiated by `AppController`. Do not enable alongside `InputPanelService` — see [puzzlebox_hw.md §3.2](puzzlebox_hw.md#32-i2c--three-devices-on-one-bus). |

## Planned / idea-stage (not built)

From [ideas.md](../ideas.md) — nothing here is wired or coded yet.

| Component | For | Notes |
|---|---|---|
| Extra RFID tags (4th+ tag / tag set) | Elemental-mixing puzzle idea | Needs matching colour tokens too |
| Physical colour tokens / gel chips | Colour-sensor puzzles | Consumable prop, not electronics |
