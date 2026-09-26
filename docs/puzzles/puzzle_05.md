# Puzzle 05 — Living Interval

Stage 5 of 7 in the [full game plan](plan.md). New design, not tied to an existing
`protocol::PuzzleId` value (see [plan.md](plan.md) §4 — needs a new `LivingInterval` value).
Carries forward the spirit of `ideas.md` #10 ("The Living Clock") — the passage of real time
as the obstacle itself — but drives the moving target from elapsed `millis()` since this
stage began rather than the physical RTC module, which is `Not connected`.

## Hook

The reactor's balance point never sits still. Catch it where it actually is right now — not
where it was a second ago, not where you expect it to drift next.

## Hardware

The same 3 rotary encoders + LEDs as stage 4, plus sound. Deliberately no new hardware —
this stage exists specifically to make the case that the same physical surface can be
meaningfully harder the second time, not just re-skinned.

## Mechanic

Unlike stage 4's fixed hidden target (set once at `begin()` and only nudged by jitter), each
encoder's target here is a continuous function of elapsed time since this stage started:

```
target_i(t) = baseline_i + amplitude_i * sin(2*pi*t / period_i + phase_i)
```

with a distinct `period_i`/`phase_i` per encoder so the three targets drift out of sync with
each other, not in lockstep. The player must bring each encoder within tolerance of its
*live* target and **hold it there continuously for a hold duration** (not just touch it once —
a moving target caught for a single tick and then drifted back out doesn't feel earned). All
three encoders must be simultaneously within tolerance for the full hold duration to count as
locked; unlike stage 4, there's no jitter-on-drift mechanic here — the target is already
moving on its own, so a second failure mode isn't needed to keep it interesting.

Confirm by pressing any encoder's button once all three have completed their hold — same
convention as stage 4.

## Difficulty tiers

| Tier | Period range | Amplitude | Tolerance | Hold duration |
|---|---|---|---|---|
| Easy | 12–18 s | small | 4 detents | 1.0 s |
| Medium | 7–11 s | medium | 2 detents | 1.5 s |
| Hard | 3–6 s | large | 1 detent | 2.0 s |

Shorter periods and larger amplitudes make the target move faster and further, which raises
the skill from "find the target" (stage 4's problem) to "predict where it's going and move
with it" — the genuine escalation this stage is built to deliver.

## Time budget

Per [plan.md](plan.md) §5: target completion window 9:30–13:00 into the shared 20:00 room
clock (3:30 budget) — the largest budget before the finale, reflecting that this is the
hardest coordination task in the sequence.

## Sound design

- **Continuous per-encoder tone**, same primitive as stage 4 (pitch from proximity), but here
  the pitch will audibly *waver on its own* even when the player isn't touching a given
  encoder, because the target itself is moving. This is the tell that separates this stage
  from stage 4 by ear alone: a static pitch means you're not moving with a live target
  correctly, a smoothly changing pitch means you are.
- **A distinct "holding" tone layer** once an encoder enters tolerance — a soft sustained
  overtone that only plays while inside the band, cutting out immediately on drift-out, so
  players get instant audio confirmation of the hold timer running without watching a digit
  countdown.
- **`Success`** on the full 3-way hold + confirm.

## Validation

`target_i(t)` computed live every `poll()` from elapsed milliseconds since `begin()` (a local
`millis()` baseline captured at stage start — no RTC read anywhere in this stage). Per-encoder
in-tolerance state tracked with its own running hold-timer that resets to zero the instant the
encoder drifts back out of band; `isSolved()` requires all three hold-timers to have reached
the tier's hold duration simultaneously, confirmed by a button press.

## Win / fail handling

- **Win:** all three sustained holds complete + confirm press → `isSolved()` true, stage 6
  begins.
- **No hard fail within the stage.** A dropped hold simply resets that encoder's timer, not
  the whole puzzle or the other two encoders' progress — losing time, not progress, consistent
  with every other stage's policy. Whole-run timeout behaviour is the open question in
  [plan.md](plan.md) §7.

## What needs to be built vs. already exists

| Piece | Status |
|---|---|
| Encoder read/LED primitives (`InputPanelService`) | **Exists** — reused from stage 4's foundation |
| Live sine-drift target computation + per-encoder hold timers | **Does not exist** — new puzzle class, structurally similar to `SpectralTunerPuzzle` but with a moving target instead of jitter |
| Continuous-tone sound (shared primitive with stage 4) | **Does not exist** — see [plan.md](plan.md) §3 item 1; building it once for stage 4 covers this stage too |
| Wiring into the sequencer | **Does not exist** — see [plan.md](plan.md) §3 item 4 |

## Why it's stage 5

The hardest pure-coordination stage in the sequence, deliberately placed right before the
real-time endurance test of stage 6 — both demand sustained attention rather than a single
correct answer, so the run's difficulty curve climbs continuously from here rather than
resetting. Reusing stage 4's exact hardware (instead of introducing a fourth physical surface)
keeps the escalation legible: same dials, harder problem.
