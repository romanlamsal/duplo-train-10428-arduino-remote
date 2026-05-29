# Pot throttle — current state & open observations

## Current state

Implemented and on `main` (see `duplo-train-10428-arduino-remote.ino`
loop):

- `armed` (bool) — set by Fwd/Back, cleared by Stop. Pot only drives
  speed while armed.
- `intendedDir` (int, ±1 / 0) — set by Fwd/Back, cleared by Stop. The
  pot-follow uses this for the sign of the motor command, *not*
  `sign(observedSpeed)` — that earlier version had a bug where pressing
  Backward while moving forward got immediately undone by the residual
  forward speed reported by the speedometer.
- `TOLERANCE = 5` — pot-follow only resends when `|pot - |speed|| > 5`.
- `readThrottle()` in `Buttons.cpp` returns `[10..100]` from GPIO 34
  (ADC1). Pot is wired with VCC/GND swapped — inversion done in
  software (`raw = 4095 - analogRead(POT_PIN)`).
- `USE_POT` flag in `Buttons.cpp` makes the pot optional: when false,
  `readThrottle()` returns `FALLBACK_THROTTLE = 65` and the ADC is
  never read. Flip before flashing if the pot isn't wired up.

`Train::observedSpeed()` returns the latest signed int8 from
speedometer notifies (port `0x36`, mode 0). Notify handler in
`Train.cpp` parses `05 00 45 36 <int8>`.

## Open: manual brake auto-disarm

Speedometer **snaps to 0** when the train is manually held or stopped
by an action brick — no decel ramp. Log from 23:21:26 (train commanded
at −50, then grabbed by hand):

```
notify: 05 00 45 36 cf   # -49
notify: 05 00 45 36 cd   # -51
notify: 05 00 45 36 d0   # -48
notify: 05 00 45 36 ca   # -54
notify: 05 00 45 36 00   # 0  ← snap, no intermediate values
```

This is an easy signal: while armed, a single `speed == 0` reading
after the train has been confirmed running in the commanded direction
means the train has been braked (somehow — by hand, by a brake brick,
doesn't matter). Reaction: cut motor + disarm so the train doesn't
fight a held grip or restart when released.

Subtlety to remember when implementing: during a Fwd↔Bwd button-driven
reversal, the wheels physically cross 0 mid-decel. A naive "speed == 0
while armed" would auto-disarm every reversal. Gate the "confirmed
running" flag on `sign(speed) == intendedDir` (e.g.
`intendedDir * speed > 0`), so the brief zero-crossing during a
reversal doesn't count.

## Open: manual push start

Speedometer **ramps up** when the train is pushed by hand from rest —
not snap-on. Log from 23:21:36 (manual push forward from rest):

```
notify: 05 00 45 36 12   # +18
notify: 05 00 45 36 33   # +51
notify: 05 00 45 36 39   # +57
notify: 05 00 45 36 3c   # +60
notify: 05 00 45 36 3d   # +61
```

That's basically indistinguishable from the ramp produced by a
button-driven start — both go 0 → small → larger → target. So
"speed went nonzero while disarmed → manual push, arm" is a false
trigger right after a Fwd/Back press where the train is just
accelerating.

To do this right, would need to either:

- correlate with "did we recently send a motor command", or
- look at how the ramp terminates (manual push tops out at whatever
  the human gave it; button-driven start should top out at the
  commanded pot magnitude), or
- give the user a way to opt in explicitly (e.g. a second pot
  position, or just decide "pot at floor means disarm; pot raised
  from floor while train moving means arm").

Lower priority than the brake-detect — it's a nice-to-have, not a
"motor fights you" problem.

## Related

README has these listed in "What this firmware doesn't (yet) do".
This note is the longer-form version so future-me has the log
evidence handy.
