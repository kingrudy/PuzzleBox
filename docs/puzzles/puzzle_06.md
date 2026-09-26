# Puzzle 06 — Reactoroverbelasting (Tetris)

Stage 6 of 7 in the [full game plan](plan.md). `protocol::PuzzleId::Tetris`. Adapts
`ideas.md` #7 ("Reactor Overload"), with the stability meter moved from the (`Removed`)
WS2812 matrix onto the display itself, since the matrix hardware no longer exists in the box.

## Hook

The existing Tetris reactor puzzle, wired into the room's other senses so a wobble in your
stacking is a wobble the whole box feels.

## Hardware

TM1638 (input — move/rotate/drop/pause, per the existing button mapping), the main display
(board rendering — `kDisplayOwnsTetris = true`, unchanged from the current design), sound,
vibration motor.

## Mechanic

Builds directly on the Tetris mechanic already described in `spec/puzzlebox_hw.md` §8 (10×16
board, display renders and reports progress via `POST /api/tetris/progress`, main controller
owns the input queue) rather than replacing it. A "stability" value ticks down whenever the
stack gets tall (board height above a threshold) and ticks back up on line clears. Unlike
`ideas.md` #7's original pitch, stability is shown as a shrinking bar drawn directly on the
display alongside the board — the matrix idea's "visible from across the room" benefit is
lost, but the display is already the biggest, brightest surface in the box, so nothing about
the tension cue's visibility is actually reduced.

Below a stability threshold:

- the vibration motor starts a slow heartbeat-like pulse that quickens as stability keeps
  dropping;
- the ambient tone (see Sound design) creeps upward in pitch.

None of this ends the puzzle on its own — it's pure tension scaffolding around the existing
win condition (reach the target level), rewarding clean, efficient play with a calmer box and
punishing sloppy stacking with an increasingly anxious one.

## Difficulty tiers

No difficulty enum exists for Tetris in code today (unlike `SpectralTunerPuzzle`'s
`Difficulty`). Proposed tiers, keyed to target level and stability sensitivity:

| Tier | Target level | Stability drain threshold (board height) | Drain rate |
|---|---|---|---|
| Easy | 3 | 12 of 16 rows | slow |
| Medium | 5 | 10 of 16 rows | medium |
| Hard | 7 | 8 of 16 rows | fast |

## Time budget

Per [plan.md](plan.md) §5: target completion window 13:00–18:00 into the shared 20:00 room
clock (5:00 budget) — the longest of any stage, matching that this is the only stage
requiring sustained real-time play rather than a single correct action.

## Sound design

- **Ambient tone tied to stability**, not to any single event — pitch rises continuously as
  stability falls, the clearest "read pitch without looking" moment in the whole sequence,
  since a player's eyes are necessarily locked on the falling board.
- **Line-clear tone** — a short bright cue distinct from the ambient drone, so clearing a line
  registers as relief even mid-tension.
- **`Success` (`Endgame`-adjacent)** on reaching the target level.
- This stage doesn't need the continuous-sweep primitive stage 4/5 introduce for anything
  new — a slowly-interpolated pitch value driven by the stability number is the same
  mechanism, just fed a different input.

## Validation

No new pass/fail logic beyond what Tetris already needs — purely a presentation layer driven
by the existing board state exposed via `/api/tetris`, per `ideas.md` #7. Stability itself is
new state (a float or int the main controller tracks alongside the board), but it never gates
win/loss on its own.

## Win / fail handling

- **Win:** target level reached → `isSolved()` true, stage 7 begins.
- **No hard fail from stability alone**, matching `ideas.md` #7's original framing — stability
  is tension, not a second win condition. Whole-run timeout behaviour (does the shared 20:00
  clock running out here end the run, given this is the single longest stage) is the sharpest
  version yet of the open question in [plan.md](plan.md) §7 — worth deciding before this stage
  is built, since it's the stage most likely to actually run the clock out.

## What needs to be built vs. already exists

| Piece | Status |
|---|---|
| TM1638 Tetris button mapping, `kTetrisPuzzle` render mode | **Exists** — `InputPanelService` |
| Tetris board rendering + progress reporting on the display | **Exists** per `spec/puzzlebox_hw.md` §8 (`kDisplayOwnsTetris`) |
| A `Tetris` puzzle class tying board state, input queue, and win condition together | **Does not exist** in `firmware/main/src/puzzles/` — only the render/input primitives are built |
| Stability meter (tracking, decay/recovery, display rendering) | **Does not exist** |
| Ambient stability-driven tone + vibration heartbeat | **Does not exist** — see [plan.md](plan.md) §3 item 1 |

## Why it's stage 6

The only stage demanding sustained real-time skill rather than solving a single static
puzzle — the natural climax before the finale, and the stage where every other stage's sound
lesson (read pitch, not the screen) pays off hardest, since the screen is fully occupied by
the board itself.
