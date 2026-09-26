# Puzzle 03 — Vibrational Cipher

Stage 3 of 7 in the [full game plan](plan.md). New design, not tied to an existing
`protocol::PuzzleId` value (see [plan.md](plan.md) §4 — needs a new `VibrationalCipher`
value). Adapts `ideas.md` #5 ("Vibrational Morse Relay"), with the hidden-sensor
touch-gating dropped since both hidden sensors are `Not connected` — the core idea (the box
transmits information through touch/feel, not the screen) survives without them.

## Hook

The reactor can't speak through the screen right now — put a hand on the case and feel for
its heartbeat instead.

## Hardware

Vibration motor (`VibrationService`, the transmitter), TM1638 (digit entry + confirm),
sound (used only as atmosphere around the transmission, not during it — see Sound design).

> **Note on the dropped hidden-sensor gating.** `ideas.md` #5's original version used the two
> hidden sensors to confirm a hand was actually touching the case, and degraded the signal for
> the "wrong" sensor to reward deliberate exploration. Neither sensor is wired in, so this
> version can't verify physical contact in firmware — it relies on the room's own briefing
> telling players to place a hand on the case, not on a sensor gate. If hidden sensors are
> added later, this stage should absorb `ideas.md` #5's gating behaviour rather than staying
> as-is.

## Mechanic

1. **Standby.** TM1638 phase digits show `STBY`. An ambient hum plays (see Sound design)
   while the box "warms up" for a randomized short delay (1–3 s) — this is deliberate: the
   player shouldn't be able to predict the exact instant transmission starts down to the
   millisecond.
2. **Transmission.** The hum cuts out — that's the cue. The vibration motor pulses a short
   numeric code in Morse (dot = short pulse, dash = long pulse, per the timing table below),
   once, with no visual or audio accompaniment. TM1638 phase shows `XMIT`.
3. **Entry.** Phase switches to `INPT`. Player enters the decoded digits on the TM1638 and
   confirms with any button.
4. **Wrong entry:** `Error` tone, phase briefly shows `ERR `, then a **re-transmission** at the
   same tier's timing (this is the stage's only "retry" mechanism — there's no way to review a
   missed pulse otherwise), minus a time penalty per the tier table.
5. **Correct entry:** `Success` tone, stage advances.

## Difficulty tiers

| Tier | Code length | Dot / dash duration | Re-transmission penalty |
|---|---|---|---|
| Easy | 3 digits | 150 ms / 450 ms | 5 s |
| Medium | 3 digits | 120 ms / 350 ms (`ideas.md` #5 baseline) | 8 s |
| Hard | 4 digits | 90 ms / 260 ms | 12 s, and the standby hum's randomized delay widens to 1–5 s |

## Time budget

Per [plan.md](plan.md) §5: target completion window 4:00–6:30 into the shared 20:00 room
clock (2:30 budget) — the largest budget of any stage so far, since a single missed pulse
genuinely does mean waiting through a full re-transmission, not just re-trying instantly.

## Sound design

This stage's whole design turns on the *absence* of sound being informative, a direct
escalation from stage 1's "sound tells you the answer":

- **Ambient standby hum**, quiet and continuous, while phase is `STBY`.
- **Hard silence** the instant transmission begins — no hum, no tone, nothing but the motor.
  A player who's internalized "the box talks to me through sound" (stage 1) has to
  consciously override that expectation and switch to touch — that moment of recalibration is
  the point of placing this stage right after two sound-forward stages.
- **`Error` / `Success`** cues on entry, same shared vocabulary as every other stage — sound
  returns immediately once the touch-only window closes.

## Validation

Target code generated once at `begin()`, encoded as a `VibrationService::pulse()` sequence
(list of durations matching the active tier's dot/dash timing) played exactly once per
transmission. TM1638 digit entry compared to the plaintext target after confirm; a mismatch
triggers a fresh transmission of the *same* target code (not a new one), so a player who
half-caught it can refine their read rather than starting from zero information.

## Win / fail handling

- **Win:** entered code matches target → `isSolved()` true, stage 4 begins.
- **No hard fail within the stage.** Unlimited re-transmissions, each with the tier's time
  penalty; whole-run timeout behaviour is the open question in [plan.md](plan.md) §7.

## What needs to be built vs. already exists

| Piece | Status |
|---|---|
| `VibrationService` (`pulse()`, `tick()`, `stop()`) | **Exists** |
| Morse-sequence encode/playback on top of `VibrationService` | **Does not exist** — needs a small scheduler queuing multiple `pulse()` calls with gaps, since the service itself is one-shot only |
| TM1638 render mode for this puzzle (`STBY`/`XMIT`/`INPT`/`ERR`) | **Does not exist** |
| Puzzle class (code generation, transmission state machine) | **Does not exist** |
| Sound / cue-push path (for the standby hum specifically) | **Does not exist** — shared prerequisite, see [plan.md](plan.md) §3 items 1–2 |

## Why it's stage 3

The first stage that asks players to distrust their ears rather than trust them — a direct,
deliberate inversion of what stages 1 and 2 just taught, which is a more interesting kind of
"harder" than simply shrinking a tolerance. Still single-surface and low-dexterity, keeping
the step up from stage 2 in *attention*, before stage 4 introduces real two-handed
coordination.
