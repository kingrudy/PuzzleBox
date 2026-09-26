# Puzzle 04 — Spectraalresonantie (SpectralTuner)

Stage 4 of 7 in the [full game plan](plan.md). `protocol::PuzzleId::SpectralTuner` — the one
stage in this sequence that's already partially implemented
(`firmware/main/src/puzzles/spectral_tuner_puzzle.h/.cpp`). Design source: `ideas.md` #1,
"The Spectral Resonance Tuner." This spec adds the timing, sound, and win/fail framing the
existing code doesn't have yet.

## Hook

Three temporal fields have drifted out of phase. Retune them by eye and by ear — all at once,
all before the field decays.

## Hardware

3 rotary encoders + their RGB LEDs (`InputPanelService`), sound. Fully connected today —
this is the only stage in the sequence with no physical hardware gap at all, only firmware
gaps (sound, sequencer, timer).

## Mechanic (as already coded)

Each encoder has a hidden target position, set relative to wherever it happened to be at
`begin()` (encoder values are a free-running accumulator with no absolute zero). The
encoder's own LED slides blue → amber → green as it nears its target
(`SpectralTunerPuzzle::colorForProximity`). All three must be within `tolerance_`
*simultaneously* to count as locked — letting a previously-locked encoder drift back out
jitters the target of the encoders still holding lock
(`jitterOtherLockedTargets`), so the last stretch is a genuine three-way balancing act, not
three independent dials. Confirm by pressing any encoder's button while all three are locked.

## Difficulty tiers (already coded)

`SpectralTunerPuzzle::Difficulty`, mapped to `tolerance_` in `begin()`:

| Tier | Tolerance (encoder detents) |
|---|---|
| `kEasy` | 4 |
| `kMedium` | 2 |
| `kHard` | 1 |

## Time budget

Per [plan.md](plan.md) §5: target completion window 6:30–9:30 into the shared 20:00 room
clock (3:00 budget) — the midpoint of the run, and the first stage genuinely difficult enough
that the target window has real slack built in for retries after a jitter cascade.

## Sound design — the missing half

`tuner_visualizer.h`'s own comment already flags this: "the audio half (a pitch sweep on this
board's I2S speaker) is a natural follow-up... `DisplaySpeakerService` still doesn't [exist]."
This spec is that follow-up:

- **Continuous per-encoder tone**, pitch mapped directly from `proximity(index)` — the same
  0 (far) .. 1 (locked) value already driving `colorForProximity`. Far = low, dissonant pitch;
  locked = a clean, stable pitch. All three tones play simultaneously, panned or timbrally
  distinguished per encoder (e.g. different waveform per encoder, matching the "encoder 3 is
  the reference field" framing in `ideas.md` #1) so a player can track "which field am I
  hearing" without looking.
- **A locked field's tone stabilizes**, giving genuine three-way ear feedback: a player
  nudging encoder 2 can hear encoder 1's tone waver the instant `jitterOtherLockedTargets`
  fires, even without looking at encoder 1's LED — this is the payoff for stage 2's
  pitch-as-proximity idea and stage 1's per-position-tone idea both landing on the same
  physical dial at once.
- **Confirm tone (`Success`)** on the committing button press once all three are locked.
- This is the stage that needs the *continuous sweep* capability of `DisplaySpeakerService`
  (not just fixed cues) — see [plan.md](plan.md) §3 item 1. It's the natural place to build
  that capability first, since stage 5 needs the same primitive.

## Validation (already coded, unchanged)

Each encoder's raw quadrature `value` (relative to its `baseline_`) compared against
`targetOffset_ ± tolerance_`; `allLocked()` requires all three simultaneously; `isSolved()`
flips true on a button press while `allLocked()`.

## Win / fail handling

- **Win:** `isSolved()` true (existing code) → stage 5 begins.
- **No hard fail within the stage.** The jitter mechanic already provides organic difficulty
  without a formal penalty system — no additional time penalty is proposed here beyond the
  time genuinely lost re-converging after a jitter. Whole-run timeout behaviour is the open
  question in [plan.md](plan.md) §7.

## What needs to be built vs. already exists

| Piece | Status |
|---|---|
| `SpectralTunerPuzzle` (targets, jitter, lock detection, difficulty) | **Exists** |
| Encoder LED colour ramp | **Exists** — `colorForProximity` |
| `/api/tuner` status endpoint + `TunerVisualizer` on the display | **Exists** |
| Continuous-tone sound (the audio half) | **Does not exist** — see [plan.md](plan.md) §3 item 1 |
| Wiring into a sequencer (currently runs unconditionally at boot) | **Does not exist** — see [plan.md](plan.md) §3 item 4 |
| Room clock feed | **Does not exist** — see [plan.md](plan.md) §3 item 5 |

## Why it's stage 4

The midpoint of the run and the first stage demanding real two-handed, three-way split
attention — a clear step up from stages 1–3's single-surface, sequential mechanics. Landing
it here also means the pitch-as-proximity idea (introduced gently in stage 2) and the
per-position tone idea (introduced in stage 1) both converge on one puzzle before stage 5
pushes the same hardware even further.
