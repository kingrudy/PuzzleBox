# Puzzle 02 — Resonant Grid

Stage 2 of 7 in the [full game plan](plan.md). New design, not tied to an existing
`protocol::PuzzleId` value (see [plan.md](plan.md) §4 — needs a new `ResonantGrid` value).
Built entirely from hardware the box already has connected: the display's capacitive touch
and its sound, both currently unused by any puzzle idea in `ideas.md`.

## Hook

A grid of dormant containment cells wakes on the big screen. There's a correct order to bring
them online — nobody wrote it down. Touch one, listen to how it responds, and let the reactor
tell you whether you're getting warmer.

## Hardware

Main display's GT911 capacitive touch + 800×480 rendering (the grid itself), sound. No other
device is required for the core mechanic — this stage is deliberately single-surface, the
same way stage 1 is TM1638-only.

## Mechanic

At `begin()`, the box privately generates a random permutation of the grid's cells — the
"correct order." Nothing on screen indicates it. The player taps any cell:

- **Correct next cell** (matches the head of the hidden order): it lights and locks in
  (stays lit, can't be re-selected), plays the next note of a rising scale — the same
  per-position vocabulary from stage 1 — and the cursor advances.
- **Any other cell:** flashes briefly and plays a tone whose *pitch* encodes how spatially
  close that cell is (Manhattan distance on the grid) to the actual correct next cell — high
  pitch close, low pitch far — the same warm/cold-by-pitch idea `SpectralTunerPuzzle` already
  uses for proximity, applied spatially instead of to a continuous dial. No cursor movement,
  no penalty on a *first* touch of a given cell — the whole point is that exploring is how you
  solve it, not a mistake to be punished lightly.
- Repeated wrong touches of cells already explored this attempt do start costing time (see
  penalty below), so idle mashing isn't free, but the first exploratory touch of each cell per
  attempt is informative, not punishing.

## Difficulty tiers

| Tier | Grid size | Order length | Repeat-touch penalty |
|---|---|---|---|
| Easy | 2×2 (4 cells) | 4 | none — unlimited free exploration |
| Medium | 3×3 (9 cells) | 9 | 5 s after the 2nd touch of the same wrong cell |
| Hard | 4×4 (16 cells) | 16 | 5 s after the 1st touch of any wrong cell |

Grid size directly controls both the search space and how informative Manhattan-distance
pitch feedback is (a 4×4 grid has much coarser distance resolution than a 2×2 one), so
difficulty compounds rather than just adding more cells to tap.

## Time budget

Per [plan.md](plan.md) §5: target completion window 2:00–4:00 into the shared 20:00 room
clock (2:00 budget). Penalties per the table above; deliberately the *lightest*-penalty stage
in the sequence, since the mechanic is explicitly built to reward exploration.

## Sound design

- **Rising scale on progress**, identical convention to stages 1 and (later) 7 — every
  correctly-ordered touch advances one note, so completing the grid sounds like completing a
  melody, same as completing a TM1638 sequence did in stage 1.
- **Distance-encoded pitch on a miss** — this is the stage's one genuinely new sound idea:
  pitch as a *spatial* hint rather than a binary right/wrong. A player who's been listening
  since stage 1 will try the pitch-guided direction before mashing randomly.
- **`Error`-family tone only after the repeat-touch penalty kicks in** — the first touch of any
  cell never sounds like a mistake, only a measurement.
- **`Success`** on the full order completed.

## Validation

Hidden target order = random permutation of cell indices, generated once at `begin()`. A
`cursor_` index into that order; each touch event compared to `order_[cursor_]`. On a match,
`cursor_++` and the cell is marked locked; the puzzle is solved when `cursor_` reaches the end.
Distance-to-target pitch on a miss is computed live from grid coordinates of the touched cell
vs. `order_[cursor_]` — no separate "hint" data structure needed beyond the same order array.

## Win / fail handling

- **Win:** full order completed → `isSolved()` true, stage 3 begins.
- **No hard fail within the stage.** Unlimited attempts on any cell; only repeated
  already-explored touches cost time, per the tier table. Whole-run timeout behaviour is the
  open question in [plan.md](plan.md) §7.

## What needs to be built vs. already exists

| Piece | Status |
|---|---|
| Display rendering (grid layout, cell states) | **Does not exist** — new display-side view alongside `TunerVisualizer` |
| Touch event capture on the display, reported to the main controller | **Does not exist** — needs the touch-event surfacing described in [plan.md](plan.md) §3 item 3 |
| Puzzle class (order generation, cursor, distance calc) | **Does not exist** |
| Sound / cue-push path | **Does not exist** — shared prerequisite, see [plan.md](plan.md) §3 items 1–2 |

## Why it's stage 2

First stage to use the touchscreen at all — every other stage in the whole sequence uses
TM1638 or the encoders for input, so this is the one moment the big screen is more than a
status mirror. It introduces "pitch as a spatial hint" early and gently (light penalties,
generous exploration) specifically so stage 4 can lean on the same idea harder without having
to also teach it from scratch.
