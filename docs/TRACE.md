# State-machine traces for the unified gesture handler

The state machine in `btn/btn.c` accepts **either** of the two memory-position
gestures and produces the same `INPUT_MEM_UP` / `INPUT_MEM_DOWN` event. This
file traces representative gestures so you can audit the state transitions by
hand.

Notation: `U` = UP pressed, `D` = DOWN pressed, `-` = released. Multiple chars
in one cell mean a single 50 ms debounced tick.

## State machine (high-level)

```
                  (U)                    (D)
                  ↓                      ↓
                [UP]   ← direction ←  [DOWN]
                  ↓                      ↓
       (both released)         (both released)
                  ↓                      ↓
   arm double-tap            arm double-tap
   window for U              window for D
                  ↓                      ↓
               [IDLE]  ←──── arm expired ──── [IDLE]
                  ↓                      ↓
       (U within 500 ms)       (D within 500 ms)
                  ↓                      ↓
   [MEM_UP] double-tap fired  [MEM_DOWN] double-tap fired
```

Additionally:

- `UP` and `DOWN` both pressed **while in `UP`** → `MEM_UP` (combo-press).
- `UP` and `DOWN` both pressed **while in `DOWN`** → `MEM_DOWN` (combo-press).
- Both buttons held: stacked thresholds — 3 s (`SAVE_HOLD_THRESHOLD=60`) →
  `SAVE`, continues counting toward 8 s; 8 s (`FACTORY_HOLD_THRESHOLD=160`) →
  `FACTORY_RESET`. Holding past 8 s fires both events on the wire; the second
  supersedes the first in EEPROM.

## Trace A — tap `△` then release, no second tap (regular up movement)

```
gesture :   U   U   -   -   -   -   -   -   -
state   : IDLE UP  UP  IDLE   |  (timer ticks, no second tap)
events  :       INPUT_UP published
                                          IDLE (no second tap, no event)
```

Latches `waiting_for_second_up = true` and the 10-tick (500 ms) window opens,
but no second `U` arrives in time, so nothing else fires.

## Trace B — combo-press: hold `△`, tap `▽` (memory-up, ivanwick style)

```
gesture :   U   U   U   D   U   -   -
state   : IDLE UP  UP  MEM_UP MEM_UP IDLE
events  :             ↑ INPUT_MEM_UP published (combo-press)
```

While in `UP`, the press of `D` (with `U` still held) jumps straight to
`MEM_UP`. The double-tap window is never armed in this path.

## Trace C — double-tap `△` within 500 ms (memory-up, DieSteinhose style)

```
gesture :   U   U   -   -   U   U   -   -
state   : IDLE UP  IDLE     UP  MEM_UP IDLE
events  :       INPUT_UP    ↑ INPUT_MEM_UP published (double-tap)
                 ↑ arm window, latch waiting_for_second_up = true
                                 ↑ second tap fires MEM_UP
```

On the first `U` release, the state machine returns to `IDLE` and arms
`waiting_for_second_up` with a 10-tick (500 ms) window. A second `U` press
within the window fires `INPUT_MEM_UP`.

## Trace D — direction change while moving (up → down without stopping)

```
gesture :   U   U   U   D   D   D
state   : IDLE UP  UP  DOWN DOWN DOWN
events  :       INPUT_UP    INPUT_DOWN
```

While in `UP`, a press of `D` (with `U` still held) transitions to `DOWN`. The
double-tap window is **not** armed because we did not go through the "both
released" transition.

## Trace E — single `▽` press (regular down movement)

```
gesture :   D   D   -   -   -   -   -
state   : IDLE DOWN DOWN IDLE  |  (timer ticks, no second tap)
events  :       INPUT_DOWN
```

Mirror of Trace A. The double-tap window is armed for `D` and expires without
firing.

## Trace F — combo-press for memory-down: hold `▽`, tap `△`

```
gesture :   D   D   D   U   D   -   -
state   : IDLE DOWN DOWN MEM_DOWN MEM_DOWN IDLE
events  :                ↑ INPUT_MEM_DOWN published
```

Mirror of Trace B. `DOWN` + `U` while in `DOWN` fires `INPUT_MEM_DOWN`.

## Trace G — double-tap `▽` within 500 ms (memory-down, DieSteinhose style)

```
gesture :   D   D   -   -   D   D   -   -
state   : IDLE DOWN IDLE     DOWN MEM_DOWN IDLE
events  :       INPUT_DOWN   ↑ INPUT_MEM_DOWN published
                 ↑ arm window
```

Mirror of Trace C.

## Trace H — both buttons held 3 s (save)

```
gesture :   U+D U+D U+D U+D U+D U+D U+D U+D U+D  ... (60 ticks)
state   : IDLE IDLE IDLE  ...                  SAVE
events  :                                ↑ INPUT_SAVE published
```

`both_hold_count` increments on every 50 ms tick while both are pressed (whether
the state is `IDLE` or `SAVE`). When it reaches `SAVE_HOLD_THRESHOLD` (60, i.e.
3 s) from `IDLE`, the state transitions to `SAVE` and fires `INPUT_SAVE`. The
counter is reset to 0 on the `IDLE → SAVE` transition so it can be re-used to
count toward the factory-reset threshold (see Trace K).

`bui_save_pos()` (called from `BUI_STOP → INPUT_SAVE`) writes the new value to
the closest of the upper/lower EEPROM slots via `eeprom_store16()` and clicks
the legs (`BCMD_CLICK`).

From `SAVE`, the gesture continues counting while both remain pressed; releasing
returns to `IDLE` (one `INPUT_IDLE` event fires).

## Trace K — both buttons held 8 s (factory-reset)

```
gesture :   U+D U+D U+D ... U+D U+D U+D U+D U+D ... (160 ticks)
state   : IDLE ........ SAVE ........ SAVE FACTORY_RESET IDLE
events  :                ↑ INPUT_SAVE                      ↑ INPUT_IDLE
                                          ↑ INPUT_FACTORY_RESET
```

Tick-by-tick:

| Tick | State | both_hold_count | Transition / event | | --------
---------------------- -----------------
-------------------------------------------- | | 1-59 | IDLE | 1..59 | none | |
60 | IDLE → SAVE | 60 → 0 | `INPUT_SAVE` published (BUI saves position) | |
61-199 | SAVE | 1..139 | none (held) | | 200 | SAVE → FACTORY_RESET | 140 → 0 |
`INPUT_FACTORY_RESET` published | | 201+ | FACTORY_RESET | 0 | stays in
`FACTORY_RESET` until release | | N | FACTORY_RESET → IDLE | (release) |
`INPUT_IDLE` event published |

`bui_factory_reset()` (called from `BUI_STOP → INPUT_FACTORY_RESET`) performs:

1. `eeprom_factory_reset()` writes `0xFFFF` (unprogrammed) to all four EEPROM
   position slots (offsets `0x00..0x07`).
2. Force `low_pos = 0` and `high_pos = 0` in memory so the next `INPUT_SAVE`
   populates both slots per the fresh-chip rule in `bui_save_pos`.
3. `bctrl_set_target(BCTRL_CLICK)` for the second audible click.

The user hears **two clicks** at the 3-s and 8-s marks. The final EEPROM state
is `0xFFFF`/`0xFFFF`/`0xFFFF`/`0xFFFF` and the in-memory state is `(0, 0)`, so
the next 3-second save behaves like a fresh-chip first save (populates both
slots).

### Counter persistence on brief releases

The counter is **not** zeroed when both buttons are released while at < 60 ticks
(pre-existing ivanwick behaviour). A user who holds to 50 ticks, releases for 5
ticks, and re-presses will reach the 60-tick threshold 15 ticks into the second
press — and fire SAVE. This is a known minor UX wart, not a state-machine bug;
same as in ivanwick.

## Trace I — combo-press then quick re-tap (no spurious second memory)

```
gesture :   U   U   U   D   -   -   U   U   -   -
state   : IDLE UP  UP  MEM_UP IDLE     UP  IDLE
events  :             ↑ INPUT_MEM_UP     INPUT_UP (NOT a memory move)
```

After the first `INPUT_MEM_UP` fires, releasing both buttons returns to `IDLE`
_without_ re-arming `waiting_for_second_up` (the latch is only set in the
`INPUT_UP` → release transition, not after a memory fire). A quick re-tap of `U`
is therefore a normal `INPUT_UP`, not a spurious second memory move.

This is the edge case that motivates the unified state machine: a pure
DieSteinhose state machine would have armed the double-tap window during the
first `UP` and then fired a _second_ `INPUT_MEM_UP` when the user happened to
press `U` again within 500 ms. The unified state machine appears to suppress
that.

## Trace J — interrupt a memory move (cancel by pressing the other button)

```
gesture :   U   U   U   D   U   D   -   -
state   : IDLE UP  UP  MEM_UP MEM_UP DOWN IDLE
events  :             ↑ INPUT_MEM_UP     INPUT_DOWN
                                          ↑ user pressed D, switch direction
```

From `MEM_UP`, pressing `D` (with `U` released or held) transitions to `DOWN`.
This is intended to let the user cancel the memory-up and start moving down
immediately — i.e. the same behaviour as in the ivanwick project.

## Re-running the traces

The state machine is exercised by `btn_gesture()` once per 50 ms debounced tick.
To sanity-check the traces, hand-simulate a few gestures by feeding `(UP, DOWN)`
boolean pairs into the `switch` statement in `btn/btn.c` and walking the state
transitions. The "InputState_t" struct is fully observable from
`btn_gesture()`'s return value plus its static state.
