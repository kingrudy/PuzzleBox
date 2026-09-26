# Puzzle 01 — Energiepatroon (Pattern)

Stage 1 of 7 in the [full game plan](plan.md). `protocol::PuzzleId::Pattern`.
Entry-level: single hardware surface, most forgiving clock, and the stage that teaches the
box's sound vocabulary the later stages lean on.

## Hook

The reactor's energy patrol runs on a fixed patrol pattern. Watch the panel light it once,
then repeat it back before the pattern decays — except now you're also expected to *listen*,
because the lights won't always be trustworthy for long.

## Hardware

TM1638 only — 8 LEDs, 8 buttons (S1–S8, all used for game input, see
`spec/puzzlebox_hw.md` §4.4), plus the 4-digit status readout for the countdown. No encoders,
no vibration, no touch — deliberately the smallest input surface in the sequence, so this
stage is about learning the box's feedback language rather than juggling hardware.

**Sound** is required and is the one dependency this spec has on unbuilt infrastructure: the
`DisplaySpeakerService` on `s3_display` (see [plan.md](plan.md) §3, item 1) and a cue-push
field on the main controller's status JSON (item 2). This puzzle does not need the continuous
pitch-sweep capability stage 4 needs — 8 fixed named tones is enough, so it can be built
against the simpler cue-based half of `DisplaySpeakerService` first.

## Mechanic

Simon-style playback/repeat, matching the tiers already documented in
`spec/puzzlebox_hw.md` §8 (not yet implemented in code — this spec is what implements them):

1. **Playback phase.** The box lights each LED in the sequence in order, on then off, timed
   by the active speed tier. Each LED-on also fires that step's tone (see Sound Design).
2. **Input phase.** Player presses the buttons in the same order. Each correct press repeats
   that step's LED + tone as confirmation. TM1638 digits show phase code `INPT` and
   `progress-target` (e.g. `2-4`), per the existing `renderPatternPuzzle()` contract in
   `InputPanelService`.
3. **Mistake.** Wrong button: LED mask flashes red-equivalent (all LEDs, since this board has
   no per-LED colour — full-mask blink), error tone plays, vibration is **not** used this
   stage (it first appears in stage 3), and a 15 s penalty is deducted from the shared room
   clock. The *same* sequence replays from the top — no new sequence is generated, so a
   mistake costs time, not progress-so-far knowledge.
4. **Success.** Full correct repeat: success tone, brief LED sweep, stage advances.

## Difficulty tiers

Two independent knobs, both already named in `spec/puzzlebox_hw.md` §8 — this spec pins them
to this stage:

| Sequence length tier | Steps |
|---|---|
| Short | 3 |
| Normal | 4 |
| Long | 6 |

| Playback speed tier | LED-on / LED-off | Error penalty |
|---|---|---|
| Easy | 780 ms / 340 ms | 15 s |
| Medium | 520 ms / 220 ms | 15 s |
| Hard | 320 ms / 140 ms | 15 s |

Stage difficulty = one length tier + one speed tier chosen together at run start (e.g. an
overall room "Easy" run = Short + Easy; "Hard" run = Long + Hard). This keeps stage 1 the
easiest point in the sequence by construction — stage 4 (SpectralTuner) already has its own
harder `kEasy/kMedium/kHard` enum, and later stages define tiers of their own in their
respective specs.

## Time budget

Per [plan.md](plan.md) §4, stage 1 draws from the shared 20:00 room clock and gets the first
and most generous window: target completion by **2:00** including any mistake penalties,
leaving 18:00 for stages 2–7. This is a soft pacing target for tuning, not a hard per-stage
reset — the clock never stops or rewinds between stages. The TM1638 `MM.SS` status readout
(already implemented, currently fed a dead `remainingSeconds_`) is what displays it live.

## Sound design

This is the stage that exists specifically to teach sound as information, so it gets the most
deliberate audio design of the three:

- **Per-step tone.** Each of the 8 LED positions maps to one note of a fixed 8-note scale
  (root note + 7 steps, reusing the "root note index + semitone offsets" phrase format
  `spec/puzzlebox_hw.md` §6.3 already describes) — so the *pattern* is audibly a short
  melody, not eight identical beeps. A player who loses track of the lights (glare, standing
  off to the side) can still recover the sequence by ear alone.
- **Confirm tone.** Reuses the same per-step note on correct input — playback and repeat
  sound identical, reinforcing that the melody *is* the answer.
- **Error tone.** One consistent `Error` cue (`AudioCueId::Error`, 740 ms) regardless of which
  step failed — deliberately generic, so it reads as "wrong," not as a hint.
- **Success tone.** `AudioCueId::Success` (420 ms) on full correct repeat.
- **Why it matters later.** Stage 2 (Resonant Grid) reuses the per-position pitch idea
  spatially (distance-to-target as pitch). Stage 3 (Vibrational Cipher) reuses its *absence*
  — silence as the cue that a transmission is starting. Stage 4 (SpectralTuner) and stage 5
  (Living Interval) both reuse it as a continuous pitch sweep instead of discrete steps.
  Stage 6 (Tetris) reuses `Error`'s harsh timbre as its rising-alarm ambient tone. A player
  who wasn't listening in stage 1 starts every later stage at a real disadvantage — that's
  intentional, not incidental.

## Validation

`SequenceBuffer` of pressed button indices compared against the generated target sequence,
same length and order requirements as described for the existing `Pattern` render mode. A
step counts only once its button press is registered by `takeLedKeyButtonPress()`; no timing
requirement within the input phase itself (players may take as long as they want between
presses — pressure comes from the room clock, not a per-button timer).

## Win / fail handling

- **Win:** all steps correct in one uninterrupted repeat → `isSolved()` true, stage 2 begins.
- **No hard fail within the stage.** Per the open retry-policy question in
  [plan.md](plan.md) §7, this stage has no attempt cap — a player can retry the same sequence
  indefinitely, and the only real cost is the shared clock via the 15 s penalty. If the
  overall room clock hits zero while still on stage 1, that's a whole-run `GameState::Timeout`,
  not a stage-specific failure — no new logic needed here beyond wiring stage 1 into the
  sequencer described in [plan.md](plan.md) §3 item 4.

## What needs to be built vs. already exists

| Piece | Status |
|---|---|
| TM1638 render mode (`kPatternPuzzle`, phase/progress digits, LED mask) | **Exists** — `InputPanelService::renderPatternPuzzle()` |
| Button reading, debounce | **Exists** — `InputPanelService` |
| `Pattern` puzzle class (sequence generation, phase state machine) | **Does not exist** — only the render/read primitives it would call are built |
| Difficulty tiers (length + speed) | **Documented only** (`spec/puzzlebox_hw.md` §8) — no code |
| Per-step tone / `DisplaySpeakerService` | **Does not exist** — see [plan.md](plan.md) §3 item 1 |
| Cue-push path main → display | **Does not exist** — see [plan.md](plan.md) §3 item 2 |
| Room clock decrement | **Does not exist** — `remainingSeconds_` is dead code, see [plan.md](plan.md) §3 item 5 |
| Puzzle sequencer to reach this stage at all | **Does not exist** — `SpectralTunerPuzzle` currently runs unconditionally at boot with nothing before or after it, see [plan.md](plan.md) §3 item 4 |

## Why it's stage 1

Smallest hardware surface (one bus-free device, no I2C contention, no two-handed
coordination), most forgiving clock, and the only stage whose entire job is to make sound
legible before it has to compete with anything else — the investment pays off directly in
every stage that follows, not as a one-off gimmick.
