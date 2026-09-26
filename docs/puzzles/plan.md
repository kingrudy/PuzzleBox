# Chronolab Puzzlebox — Full Game Plan

Design plan for a **7-stage sequence built entirely from hardware currently marked
`Connected`** in [spec/components.md](../../spec/components.md) — no RFID, no RTC module, no
hidden sensors, no servo, no colour sensor, no WS2812 matrix (`Removed`). Each stage is
harder and more time-pressured than the last, with sound as a first-class feedback channel
throughout, not just success/error beeps.

> **Why this departs from `protocol::PuzzleId`.** The codebase's existing enum
> (`Pattern, Rfid, Rtc, Hidden, SpectralTuner, Tetris, FinalLock`) assumes hardware this box
> doesn't have wired in today. Rather than spec stages the box can't actually run, this plan
> keeps `Pattern`, `SpectralTuner`, and `Tetris` as-is and replaces the four
> hardware-blocked slots with new stages built from the same connected surfaces
> (TM1638, 3 encoders, vibration motor, the display's touch + sound) used in different ways.
> **`PuzzleId` will need new values** for the replacement stages — see §4.

Individual stage specs live alongside this file, one per stage:
[puzzle_01.md](puzzle_01.md) · [puzzle_02.md](puzzle_02.md) · [puzzle_03.md](puzzle_03.md) ·
[puzzle_04.md](puzzle_04.md) · [puzzle_05.md](puzzle_05.md) · [puzzle_06.md](puzzle_06.md) ·
[puzzle_07.md](puzzle_07.md).

---

## 1. Hardware budget (hard constraint — nothing outside this list)

Per [spec/components.md](../../spec/components.md), today's `Connected` rows, and *only*
these:

| Hardware | Service | Notes |
|---|---|---|
| TM1638 (8 digits, 8 LEDs, 8 buttons) | `InputPanelService` | universal I/O surface |
| Rotary encoder 1 (input + bi-colour LED) | `InputPanelService` | no blue channel |
| Rotary encoder 2 (input + bi-colour LED) | `InputPanelService` | no blue channel |
| Rotary encoder 3 (input + full RGB LED) | `InputPanelService` | |
| Vibration motor | `VibrationService` | one-shot pulses only |
| Main display (800×480 + GT911 touch) | display node | `s3_display` only |
| Main display I2S audio out | — | physically connected; `DisplaySpeakerService` doesn't exist in code yet (§3) |

Everything else — RC522/RFID, RTC module, both hidden sensors, the TCS3200 colour sensor, the
servo, both WS2812 matrices — is `Not connected` or `Removed`. No stage below reads from or
writes to any of them. `millis()`-based elapsed time (used by stage 5) is not a device and
doesn't appear in `components.md` at all — it's core MCU functionality, always available,
which is exactly why it substitutes for the physical RTC rather than needing one.

## 2. The seven stages

| Stage | Name | Core mechanic | Hardware | Origin |
|---|---|---|---|---|
| 1 | Energiepatroon (Pattern) | Simon-style sequence recall | TM1638 | `PuzzleId::Pattern`, unchanged |
| 2 | Resonant Grid | Touch a spatial order into existence via warm/cold sound | 800×480 touchscreen + sound | new — replaces the RFID-dependent slot |
| 3 | Vibrational Cipher | Decode a felt Morse-style pulse, enter it on TM1638 | vibration motor + TM1638 | adapts `ideas.md` #5, drops hidden-sensor gating |
| 4 | Spectraalresonantie (SpectralTuner) | 3-way simultaneous dial-in | 3 encoders + LEDs + sound | `PuzzleId::SpectralTuner`, `ideas.md` #1, unchanged, already partially built |
| 5 | Living Interval | Catch a continuously drifting target, held steady | 3 encoders + LEDs + sound | new — replaces the RTC-dependent slot, reuses `ideas.md` #10's "living clock" idea without the RTC chip |
| 6 | Reactoroverbelasting (Tetris) | Real-time stacking under a decaying stability meter | TM1638 + display + sound + vibration | `PuzzleId::Tetris`, `ideas.md` #7, stability meter moved from the removed matrix onto the display |
| 7 | Eindsequentie (Finale) | Recall assembled fragments from every prior stage, confirm | TM1638 + display + sound + vibration | new — replaces the servo-dependent `FinalLock`; no physical release, ends the run as `GameState::Success` |

Stages 2, 3, 5, and 7 are original designs built specifically to stay inside the hardware
budget in §1. Stages 1, 4, and 6 keep their existing `PuzzleId` identity and design lineage.

## 3. Prerequisites this plan depends on (not yet built)

The codebase today is a hardware skeleton — `AppController` explicitly says "the six
puzzles, difficulty, scoring, the full operator web UI" aren't implemented, and only
`SpectralTunerPuzzle` is wired up, live unconditionally at boot with nothing before or after
it. Shared infrastructure every stage depends on, to build once rather than per-stage:

1. **`DisplaySpeakerService` (I2S, on `s3_display`).** Referenced throughout
   `spec/puzzlebox_hw.md` §6.3 and in `protocol::AudioCueId`, but there is no implementation
   in `firmware/` — `display/src/main.cpp` has a standing TODO for it. Needs both the
   fixed-cue path (stages 1, 3, 6, 7) and a continuous-tone/sweep path (stages 2, 4, 5, and
   useful for stage 6's rising alarm).
2. **A cue-push path, main controller → display.** The display currently only *polls*
   `GET /api/status` every 750 ms; there's no field for "play this now." Needs a pending
   `AudioCueId` (or richer tone descriptor) added to that JSON and cleared once consumed,
   mirroring how `TunerVisualizer` already polls `/api/tuner`.
3. **Touch input surfaced to the main controller.** Stage 2 needs touch coordinates/events
   from the display, which today only renders — the display would own hit-testing against its
   own grid layout and report a "cell N touched" event back over HTTP, the same direction
   `POST /api/tetris/progress` already reports Tetris board state back.
4. **A puzzle sequencer.** `AppController` has no `Briefing → Active` gating and no concept
   of "current puzzle." Needs a stage cursor that calls each stage's
   `begin()`/`poll()`/`isSolved()` in turn and advances on success, plus a shared `Puzzle`
   interface — none exists today; `SpectralTunerPuzzle` is a standalone type with its own ad
   hoc shape.
5. **A working countdown timer.** `remainingSeconds_` exists on `AppController` but is never
   decremented — dead code today. Needs a real whole-room clock; the TM1638 `MM.SS` rendering
   in `kStatus` mode already exists and is ready to receive real values. Stage 5 also needs
   this clock's elapsed-time value (or its own `millis()` baseline) for its drifting target.

## 4. `PuzzleId` enum change needed

`protocol::PuzzleId` (`game_state.h`) currently has `Rfid`, `Rtc`, `Hidden`, `FinalLock`
values this plan doesn't use. Replace them with `ResonantGrid`, `VibrationalCipher`,
`LivingInterval`, and `Finale` (keeping `Pattern`, `SpectralTuner`, `Tetris` as-is), and update
`kPuzzleCount` accordingly. This is a code change, not just a doc note — anything that
switches on `PuzzleId` (diagnostics, the sidecar's status mirroring) needs the new values too.

## 5. Room clock

One continuous **20:00** clock, shared across all 7 stages — no stage hard-resets it, and no
stage gets its own independent timer.

| Stage | Target window | Budget |
|---|---|---|
| 1 Pattern | 0:00 – 2:00 | 2:00 |
| 2 Resonant Grid | 2:00 – 4:00 | 2:00 |
| 3 Vibrational Cipher | 4:00 – 6:30 | 2:30 |
| 4 SpectralTuner | 6:30 – 9:30 | 3:00 |
| 5 Living Interval | 9:30 – 13:00 | 3:30 |
| 6 Tetris | 13:00 – 18:00 | 5:00 |
| 7 Finale | 18:00 – 20:00 | 2:00 |

These are pacing targets for tuning difficulty tiers, not hard per-stage cutoffs. Mistakes
bite into the shared clock via each stage's own penalty, never a full reset.

## 6. Escalation design

- **Time tightens, not just difficulty.** Later stages get a smaller effective share of what
  remains, and by stage 6 the clock itself is presented as a stressor (rising-pitch ambient
  tone, quickening vibration heartbeat), not a static digit readout.
- **Sound is taught, then trusted, then relied on.** Stage 1 establishes a clean cue
  vocabulary (distinct pitch per LED step) with nothing else competing for attention. Stage 2
  turns pitch into spatial warm/cold feedback. Stage 3 makes silence itself meaningful (an
  ambient hum that stops right before a transmission starts). Stage 4 turns the vocabulary
  into a continuous proximity signal under split attention across 3 encoders. Stage 5 pushes
  that further — the target itself moves, so pitch alone tells you which way to correct, not
  just how close you are. Stage 6 turns it into ambient tension read without looking. Stage 7
  is the callback — the exact playback/repeat shape from stage 1, now applied to fragments
  earned across the whole run.
- **Hardware is reused, not just added to.** Stages 1 and 6 both use TM1638; stages 4 and 5
  both use the 3 encoders, with stage 5 deliberately harder on the *same* physical surface
  (a moving target held under sustained tolerance, instead of a fixed target caught once) —
  escalation comes from mechanic complexity, not from an ever-growing hardware list.
- **Mistakes cost time, not attempts.** No stage hard-resets the room clock or sends the
  player back to stage 1 on a wrong input; each stage's own penalty (documented in its own
  spec) bites into the shared 20:00 budget instead.

## 7. Open decision for the user

**Retry policy on a stage timeout** isn't decided yet: does the box hard-fail the whole run
(`GameState::Timeout`, no continue) if the 20:00 clock runs out mid-stage, or does each stage
get a bounded number of retries before that happens? This affects the sequencer design (§3
item 4) and stage 6's tuning in particular (`ideas.md` #7 has no fail condition today, only a
tension layer). Flagging it here rather than guessing.

## 8. File plan

| File | Stage | Status |
|---|---|---|
| `docs/puzzles/plan.md` | — | this file |
| `docs/puzzles/puzzle_01.md` | 1 — Pattern | written |
| `docs/puzzles/puzzle_02.md` | 2 — Resonant Grid | written |
| `docs/puzzles/puzzle_03.md` | 3 — Vibrational Cipher | written |
| `docs/puzzles/puzzle_04.md` | 4 — SpectralTuner | written |
| `docs/puzzles/puzzle_05.md` | 5 — Living Interval | written |
| `docs/puzzles/puzzle_06.md` | 6 — Tetris | written |
| `docs/puzzles/puzzle_07.md` | 7 — Finale | written |
