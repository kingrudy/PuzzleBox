# Puzzle 07 — Eindsequentie (Finale)

Stage 7 of 7 in the [full game plan](plan.md). New design, not tied to an existing
`protocol::PuzzleId` value (see [plan.md](plan.md) §4 — needs a new `Finale` value, replacing
`FinalLock`). The canonical `FinalLock` opens a physical lock via the servo; the servo is
`Not connected`, so this stage cannot do that. It closes out the run as a `GameState::Success`
payoff instead — a deliberate, honest substitution rather than a puzzle pretending to unlock
something the box can't physically unlock yet.

## Hook

Every stage handed you a fragment without saying so. Play them back in order, and the reactor
comes fully online.

## Hardware

TM1638 (playback + re-entry, identical convention to stage 1), sound, vibration motor,
display (celebration banner). No servo, no physical release.

## Mechanic

Each of stages 1–6 secretly derives one digit (0–7, matching the TM1638's 8 buttons) from
its own internal random seed at the moment it's solved — not shown to the player at the
time, just cached by the main controller. This stage is a deliberate structural callback to
stage 1:

1. **Playback phase.** TM1638 lights and sounds the 6 collected digits in the order their
   stages were solved — same LED-on/tone-per-step convention as stage 1's playback, just with
   digits instead of an arbitrary sequence.
2. **Input phase.** Player re-enters the 6 digits on the TM1638 keypad, confirms with any
   button.
3. **Wrong entry:** `Error` tone, small 5 s penalty, sequence replays from the top — identical
   failure handling to stage 1, right down to the penalty being the lightest of any
   "recall and repeat" stage in the game, since by this point a mistake is a slip, not a
   knowledge gap.
4. **Correct entry:** the full-cast celebration — `Endgame` cue, a TM1638 LED sweep, vibration
   success pulse, and the display shows a completion banner. `GameState::Success`.

This is deliberately still an *active* step, not a cutscene — the request for "each one
getting harder" throughout the sequence doesn't mean the last stage should require zero
player action, just that it shouldn't introduce new difficulty. Recalling 6 digits after a
20-minute run is a genuine (if gentle) capstone on its own.

## Difficulty tiers

None. This stage doesn't scale with the room's overall difficulty setting — 6 digits is fixed
by construction (one per prior stage), and the playback timing is deliberately the most
generous in the game (matching stage 1's Easy tier: 780 ms / 340 ms) regardless of what
difficulty the earlier stages ran at. This is the payoff, not another skill gate.

## Time budget

Per [plan.md](plan.md) §5: target completion window 18:00–20:00 into the shared 20:00 room
clock (2:00 budget) — intentionally comfortable, since by this point the run's difficulty has
already peaked at stage 6.

## Sound design

The full-circle stage: every sound idea used earlier reappears once, briefly, rather than
introducing anything new.

- **Per-digit tone during playback**, the exact stage-1 convention, reused verbatim — the
  player should recognize this immediately as "the same kind of thing as the very first
  puzzle."
- **Confirm tone per correct re-entered digit**, same as stage 1.
- **`Endgame`** cue (980 ms, the longest defined cue) on final success — the one cue in
  `protocol::AudioCueId` that hasn't been used by any earlier stage, reserved specifically for
  this moment.

## Validation

6-element digit array, one per stage, appended in solve order by whatever the sequencer (§3
item 4 in [plan.md](plan.md)) calls when a stage's `isSolved()` flips true. Playback and
re-entry validation reuse the exact same buffer-compare logic as stage 1's `Pattern` puzzle —
structurally, this stage's implementation should share code with stage 1's, not duplicate it.

## Win / fail handling

- **Win:** all 6 digits re-entered correctly → `GameState::Success`, run ends.
- **No hard fail.** Unlimited retries, 5 s penalty per wrong entry. If the shared 20:00 clock
  expires during this stage specifically, that's the most forgiving possible timeout to design
  around, since the player has already cleared every real challenge — worth folding into
  whatever the [plan.md](plan.md) §7 retry-policy decision ends up being (e.g. this stage
  alone could reasonably be exempt from a hard timeout, given how little is left to prove).

## What needs to be built vs. already exists

| Piece | Status |
|---|---|
| Per-stage digit derivation + caching in the sequencer | **Does not exist** — depends on the sequencer itself, [plan.md](plan.md) §3 item 4 |
| Playback/re-entry logic | **Does not exist** as a shared component, though it should reuse stage 1's `Pattern` implementation directly once that exists |
| Completion banner on the display | **Does not exist** |
| Sound / cue-push path | **Does not exist** — shared prerequisite, see [plan.md](plan.md) §3 items 1–2 |

## Why it's stage 7

Every stage before this one taught the player something (a sound vocabulary, a warm/cold
instinct, distrust-your-ears, two-handed coordination, prediction under a moving target,
reading pressure without looking) — this is the only stage that asks for nothing new, just a
last recall of the whole run, which is the right note to end on rather than another
difficulty spike.
