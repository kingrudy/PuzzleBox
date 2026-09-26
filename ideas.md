# Chronolab-X13 — Puzzle Concepts

Ten fully worked puzzle ideas for the Chronolab Puzzlebox, built entirely from hardware
already inventoried in [spec/puzzlebox_hw.md](spec/puzzlebox_hw.md). The colour sensor and
three rotary encoders are explicitly **not yet bound to a puzzle** — several ideas below
exist specifically to put them to work. Each entry gives the narrative hook, the hardware
combo, the moment-to-moment mechanic, how the box validates a solution, and how to scale
difficulty.

---

## 1. The Spectral Resonance Tuner

**Hook:** Three temporal fields have drifted out of phase. Retune them by ear, by eye, and
by feel — all at once, all before the field decays.

**Hardware:** 3 rotary encoders + PCA9685 encoder LEDs, display speaker (I2S, `s3_display`
only), TM1638 for phase lock-in.

**Mechanic:** Each encoder controls one "field" — turning it sweeps a tone up and down in
pitch on the display speaker while the encoder's own LED slides from cold blue through
amber to green as the tone approaches its hidden target frequency. All three fields must be
locked (green, in-tune) *simultaneously* — drifting one back out re-desyncs the others by a
small random offset, so the last stretch is a genuine three-handed balancing act. Press any
encoder's button while all three glow green to commit.

**Validation:** Each encoder's raw quadrature `value` is compared against a target band
(`targetValue ± tolerance`); tolerance shrinks per difficulty tier. All three must be in-band
on the same poll cycle.

**Why it's special:** The room has three RGB encoders and one has no puzzle. This uses turn
*feel*, LED colour, and audio pitch together — a genuinely multi-sensory dial-in that no
existing puzzle attempts, and it's the single best use of the "no blue channel on encoders
1/2" quirk (encoder 3's true RGB becomes the "reference" field the other two are tuned
against).

---

## 2. Prism Cipher

**Hook:** A shattered spectrum. Reassemble the sequence with light in your hand, not a
button under your thumb.

**Hardware:** WS2812 matrices (sequence playback), TCS3200 colour sensor (player input),
TM1638 (confirm/retry), a physical prop: 4–6 coloured tokens/gel chips (red, green, blue,
yellow, white) kept in a tray beside the box.

**Mechanic:** The matrix flashes a Simon-style colour sequence, exactly like the existing
`Pattern` puzzle — except the player doesn't press an LED button to answer. They pick up the
matching physical colour token and hold it in front of the TCS3200 sensor for it to read.
Getting the *order* right isn't enough — each hold must register a clean, stable reading
(no ambient light bleed, no wobble) before the next token is accepted, which turns "know the
sequence" into "handle real objects carefully under time pressure."

**Validation:** `ColorSensorService::detectedColor()` sampled continuously; a token counts
once its colour is stable for ≥3 consecutive 250 ms samples. Sequence buffer compared to the
generated target, same length/timing rules as the existing pattern puzzle (`Short`/`Normal`/
`Long`, difficulty-scaled playback speed).

**Why it's special:** Turns a screen-based memory puzzle into a tactile, physical-object
puzzle using the one sensor in the box that's currently idle in diagnostics-only mode.

---

## 3. Energy Conduit

**Hook:** Route power through the reactor's core in the correct order, or watch it overload.

**Hardware:** RC522 + 4–5 RFID tags ("energy cores"), WS2812 matrix (circuit visualisation),
vibration motor (overload feedback), servo (progressive latch release).

**Mechanic:** Unlike the existing single-tag `Rfid` puzzle, this uses multiple tags that must
be scanned in a *specific causal order* (e.g. Core C before Core A, Core A before Core B —
a small dependency graph, not a flat sequence). Each correct scan lights one more segment of
the matrix as "energy" visibly flows from left half to right half. A scan that's technically
a valid tag but violates the dependency order triggers a short vibration "surge" and resets
the current flow (but not the whole puzzle — partial credit persists per difficulty tier).
On full correct routing, the servo doesn't snap straight to `Open` — it steps through two
intermediate positions ("latches disengaging") before the final release, so success feels
mechanical, not electronic.

**Validation:** Small dependency graph (adjacency rules) held in `runtime_`; each
`RfidService::takeEventTag()` event checked against "have all prerequisites for this tag
already been scanned."

**Why it's special:** Reframes RFID from "one right card" into a constraint-satisfaction
puzzle, and gives the servo a moment to act instead of just a binary open/close.

---

## 4. The Chronofield Balance

**Hook:** Balance today's field against the lab's reactor. The target moves — literally —
with the calendar.

**Hardware:** 3 rotary encoders, WS2812 matrix (as a live bar graph), RTC (target seed).

**Mechanic:** The matrix splits into three vertical bars (one per encoder). Each encoder's
turn count maps directly to bar height in real time. The target isn't fixed in firmware —
it's derived from the RTC's current date (e.g. day-of-month → bar 1 target, month → bar 2,
a checksum of both → bar 3), so the correct dial-in is genuinely different every day the box
is played. A faint fourth "ghost" set of matrix pixels shows the target heights, visible only
as dim reference marks, so players must read subtlety, not just chase bright colour.

**Validation:** `RtcService::formattedNow()` seeds three target values once at `Briefing`
start (cached so it doesn't drift mid-run); each encoder's `value` compared against its
target band on every poll.

**Why it's special:** The only puzzle in the room whose *answer* — not just its difficulty —
changes daily, using the battery-backed RTC as more than a countdown source. Reinforces the
"Chronolab" premise that the room is a living instrument, not a static prop.

---

## 5. Vibrational Morse Relay

**Hook:** The reactor can't speak — it can only shake. Put your hand on the case and listen
with your fingers.

**Hardware:** Vibration motor (message transmitter), both hidden sensors (contact
confirmation), TM1638 (final code entry).

**Mechanic:** At a scripted moment the vibration motor pulses out a short message in Morse
(dot = 120 ms pulse, dash = 350 ms pulse, gaps between letters) — but only while a player is
physically touching one of the two hidden sensors (grounding a hand against the panel, which
is also how the felt vibration is strongest). Covering the *wrong* sensor gets a deliberately
muffled, harder-to-read version — steering blind guessing toward deliberate exploration of
the box's surface. The decoded message is a 3–4 digit number entered on the TM1638 keypad.

**Validation:** `HiddenTriggerService` state gates whether `VibrationService::pulse()`
playback proceeds at full or attenuated timing; the TM1638 entry is checked against the
Morse-encoded target, generated once per run and logged (not just hardcoded) so it can rotate
across playthroughs.

**Why it's special:** The vibration motor is currently pure feedback (confirm/error/warning
pulses). This is the only idea that makes it a primary *information channel* — a puzzle you
solve by touch alone, which no other room mechanic offers.

---

## 6. Temporal Duet

**Hook:** No single operator can stabilise two timelines alone. It takes two sets of hands,
moving together.

**Hardware:** Both hidden sensors (simultaneous two-point contact), TM1638 buttons
(choreographed sequence), WS2812 matrix (shared countdown/rhythm cue).

**Mechanic:** A forced two-player beat: the matrix pulses a steady rhythm (like a metronome,
speeding up per difficulty tier). On each pulse, one player must be actively covering *both*
hidden sensors at once (meaning they physically can't also reach the TM1638 — the box is
built so the sensors and the keypad are not reachable by one person at the same time) while a
second player presses the TM1638 buttons in the displayed order, in time with the beat.
Missing a beat — sensor released early, button pressed off-rhythm — drops progress back one
step rather than resetting entirely, keeping it forgiving enough to stay fun under pressure.

**Validation:** `HiddenTriggerService::sensor1Active() && sensor2Active()` gates whether the
current beat's `InputPanelService` button press is accepted; a rolling progress counter
persists partial success.

**Why it's special:** Every other puzzle in the room can technically be solved solo. This one
structurally can't — it's the room's forced-co-op moment, built from hardware that's
otherwise purely single-player (hidden sensors) plus hardware that's purely input (TM1638).

---

## 7. Reactor Overload (Tetris, Raised Stakes)

**Hook:** The existing Tetris reactor puzzle, wired into the room's other senses so a wobble
in your stacking is a wobble the whole box feels.

**Hardware:** Existing Tetris puzzle (display + TM1638 input), WS2812 matrix (stability
meter), vibration motor (rising alarm), display speaker (rising-pitch alarm tone).

**Mechanic:** Builds directly on the shipped Tetris puzzle rather than replacing it. A new
"stability" value ticks down whenever the stack gets tall (board height above a threshold)
and ticks back up on line clears. The WS2812 matrix mirrors that stability as a shrinking
green-to-red bar across both halves in real time — visible from across the room, not just on
the TM1638's two digits. Below a stability threshold, the vibration motor starts a slow
heartbeat-like pulse that quickens as stability keeps dropping, and the display speaker's
ambient tone creeps upward in pitch. None of this ends the puzzle — it's pure tension
scaffolding around the existing win condition (reach target level), rewarding clean,
efficient play with a calmer box and punishing sloppy stacking with an increasingly anxious
one.

**Validation:** No new pass/fail logic — purely a presentation layer driven by the existing
Tetris board state already exposed via `/api/tetris`.

**Why it's special:** The cheapest idea to build (no new win condition, no new sensor
binding) and the highest leverage: it makes the room's most screen-bound puzzle physically
felt, using three already-idle-during-Tetris outputs (matrix, vibration, speaker) that
currently do nothing while Tetris is being played.

---

## 8. Elemental Fusion

**Hook:** Four unstable elements. Only one combination fuses clean — the rest fail loudly.

**Hardware:** RC522 + 4 RFID tags ("elements"), TCS3200 colour sensor, a physical mixing
prop (small dish + 4 different-coloured liquid-look gel tokens or LED-lit vials matched to
the tags), WS2812 matrix (fusion progress).

**Mechanic:** Each of the 4 element tags is printed/labelled with a colour. Scanning a tag
"selects" that element and lights a matching hint colour band on the matrix. Players must
work out — via clues elsewhere in the room — the *subset* of 2–3 elements that fuses
correctly, then physically combine the corresponding coloured tokens in the mixing dish and
present the resulting *blended* colour to the TCS3200 sensor. Scan the right RFID subset but
present the wrong physical blend (or vice versa) and neither half validates — both the
logical (RFID) and physical (colour) answer must agree, so this is deliberately two puzzles
that must independently arrive at the same conclusion.

**Validation:** RFID subset checked against a target set (order-independent, unlike Energy
Conduit); `ColorSensorService::classifyColor()` result checked against the expected blended
hue independently. Both must be true within the same active phase.

**Why it's special:** No other idea here cross-validates two entirely different sensors
against the *same* answer — it's a built-in lie detector against guessing, since brute-forcing
the RFID order without understanding the underlying logic still fails the colour check.

---

## 9. Fractured Signal

**Hook:** The final code was never in one place. It's scattered across every screen in the
lab, and they only show their piece for a few seconds at a time.

**Hardware:** Sidecar OLED, main display (800×480), round display (240×240), TM1638 (final
entry), WS2812 matrix (global "signal window" cue).

**Mechanic:** The endgame meta-puzzle. Once triggered, each of the three remote nodes begins
independently cycling through mostly-decoy content, but on a shared timing window (driven off
the same `kAccessPointChannel`-synced heartbeat already used for ESP-NOW/HTTP polling) all
three simultaneously flash one true digit of a 6-digit code for ~2 seconds before returning to
decoys. Players must physically station themselves at all three displays (they're spread
around the room by design) and call out/write down what they see the instant the WS2812
matrix flashes white as the "window open" cue, then run to the TM1638 to enter the combined
code before the next window (which reshuffles which decoys show).

**Validation:** No new sensor — pure choreography across the HTTP/JSON status snapshots and
ESP-NOW heartbeat channel the network layer already provides; the "true digit" assignment and
window timing live in the main controller's runtime state, broadcast to all three nodes.

**Why it's special:** The only idea that treats the room's *networking* — four independent
nodes staying in sync — as the puzzle mechanism itself, not just plumbing. It's the natural
finale precisely because it needs every other node in the system working correctly to even
attempt it.

---

## 10. The Living Clock

**Hook:** The reactor's core has been running since it was built. It doesn't reset when you
walk in — you have to catch it where it already is.

**Hardware:** RTC (continuous, real elapsed time — not the game countdown), 3 rotary
encoders, WS2812 matrix, TM1638 status digits.

**Mechanic:** A slow-burn ambient puzzle that runs in the background from the moment the box
is powered on, independent of any single game session — using the RTC's actual battery-backed
clock rather than the per-run countdown timer. A hidden target position for the three
encoders advances on its own, deterministically, based on real elapsed seconds since a fixed
epoch (e.g. "target = f(seconds since Jan 1, kept in the RTC)"), so it's the same for every
group that plays that day but different across days and slowly drifts even within a single
session if players dawdle. Players get periodic, deliberately coarse hints (a matrix colour
temperature, a TM1638 digit) rather than a direct readout, and must "catch" the encoders to
the live target within a tolerance window rather than a fixed one.

**Validation:** Target computed on demand each poll from `RtcService::formattedNow()` (or
raw epoch seconds), not cached at puzzle start — genuinely live, not just seeded once.

**Why it's special:** Every other puzzle in this list (and in the shipped room) is static
once it starts. This is the one idea that makes the passage of *real* time itself the
obstacle — thematically the sharpest fit for a room called Chronolab, and the only use of the
RTC as a continuously moving target rather than a one-shot check or a display countdown.

---

## Hardware coverage at a glance

| Hardware | Ideas using it |
|---|---|
| 3 rotary encoders + LEDs | 1, 4, 10 |
| TCS3200 colour sensor | 2, 8 |
| RC522 + RFID tags | 3, 8 |
| WS2812 matrices | 2, 3, 4, 6, 7, 9, 10 |
| Hidden sensors | 5, 6 |
| Vibration motor | 3, 5, 7 |
| TM1638 (display/buttons) | all — the room's universal input/output surface |
| RTC | 4, 10 |
| Servo | 3 |
| Display speaker (I2S) | 1, 7 |
| Sidecar OLED + both displays as a network | 9 |

Ideas 1, 2, and 8 are the strongest picks if the goal is specifically to finally give the
colour sensor and encoders a puzzle. Idea 9 is the strongest finale candidate since it is the
only one that requires the whole four-node system to already be working correctly.
