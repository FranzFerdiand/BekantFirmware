# Test scenarios

These are the hold-duration scenarios the button state machine in
`src/btn/btn.c` is designed to handle. Tick rate is 50 ms (20 Hz) — see
`btn/btn.c::btn_init()` for the Timer2 setup.

| Hold duration         | State transitions                                      | Events fired                            | Final EEPROM (low, high)   |
| --------------------- | ------------------------------------------------------ | --------------------------------------- | -------------------------- |
| release at 50 ticks   | IDLE (count 1-49) → IDLE                               | (none)                                  | unchanged                  |
| release at 60 ticks   | IDLE (1-59) → IDLE → SAVE (60th tick) → IDLE           | `INPUT_SAVE`                            | per the save rules below   |
| release at 100 ticks  | IDLE (1-59) → IDLE → SAVE → SAVE → IDLE                | `INPUT_SAVE`                            | per the save rules below   |
| release at 159 ticks  | IDLE → SAVE (60) → SAVE → IDLE                         | `INPUT_SAVE`                            | per the save rules below   |
| release at 160 ticks  | IDLE → SAVE (60) → SAVE → FACTORY_RESET (160) → IDLE   | `INPUT_SAVE` + `INPUT_FACTORY_RESET`    | cleared to 0xFFFF / (0, 0) |
| release at 250 ticks  | IDLE → SAVE → FACTORY_RESET → IDLE                     | `INPUT_SAVE` + `INPUT_FACTORY_RESET`    | cleared to 0xFFFF / (0, 0) |
| release immediately   | IDLE                                                   | (none)                                  | unchanged                  |
| release twice quickly | press-release-press-release with total held ticks < 60 | (none — counter resets to 0 on release) | unchanged                  |

Notes on the table:

- **`INPUT_SAVE` (3 s mark):** The desk's relay clicks once. The current encoder
  position is written into one of the two EEPROM slots per the rules below.
- **`INPUT_FACTORY_RESET` (8 s mark):** The desk's relay clicks a second time.
  All four EEPROM position slots are written with `0xFFFF` and the in-memory
  `low_pos` / `high_pos` are forced to `(0, 0)`, so the next 3-second
  both-buttons-hold behaves like a fresh-chip first save (populates both slots
  per the save-rules table below).
- **Why two events on an 8-s hold:** The state machine uses **stacked
  thresholds** rather than "either/or" — the SAVE event fires at the 60-tick
  mark and the FACTORY_RESET event fires at the 160-tick mark. This gives the
  user two distinct audible confirmation clicks (3 s then 8 s) and matches the
  spirit of the original IKEA firmware's behaviour
  (`oem_reconstruction/docs/FACTORY_RESET.md`).
- **Counter reset on release:** if the user holds, releases, then re-presses,
  the hold counter is reset to 0 on each release so the next gesture starts
  fresh. (Earlier versions carried the count across releases, which could fire a
  spurious save or factory-reset immediately on a brief re-press.)
- **A full 8-s hold is two events; the final state is the fresh-chip state**
  (`(0, 0)` in memory, `0xFFFF` in EEPROM), since `INPUT_FACTORY_RESET` fires
  after `INPUT_SAVE` and overwrites the saved value at the second click.

## Save rules — stage model

The BUI tracks three explicit "stages" based on the two stored memory positions.
Each stage has its own behaviour for storing and for recalling a memory
position:

| Stage | Storage                                     | Behaviour on a 3-s save                                                                                      | MEM_UP / MEM_DOWN behaviour                                                                                                                                   |
| ----- | ------------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **0** | `low = 0`, `high = 0` (fresh or post-reset) | Populates **both** slots with the new value.                                                                 | Both buttons are ignored until the first save.                                                                                                                |
| **1** | `low = high = X` (one position)             | Smaller-than-stored → LOW slot; larger-or-equal → HIGH slot. Low and high then differ, advancing to Stage 2. | Either button drives the desk toward `X` from whichever side it is on.                                                                                        |
| **2** | `low < high` (two distinct positions)       | Closer-to-the-smaller → LOW (incl. when save < both). Closer-to-the-bigger → HIGH (incl. when save > both).  | MEM_UP drives to `high`; MEM_DOWN drives to `low`. **Either button works from any starting position** — the desk converges on the target from above or below. |

The buttons always accept in Stages 1 and 2; the press declares an intent ("go
to upper" / "go to lower") and the firmware chooses the driving direction so the
desk always lands on the recalled position. The MEM_* state's stop check uses
the requested target (rather than the default `high_pos`/`low_pos`), which is
what lets the same state drive up _or_ down depending on where the desk started.

If `cur_pos` is already within `TARGET_DEADBAND` (0x10 encoder units, ~3% of the
desk's range) of the target, the press is a no-op. This prevents the "second
press of the same memory button slightly drifts the desk away from target"
effect: tiny encoder- overshoot residuals from the first press (e.g., `cur_pos`
at `target + 1`) would otherwise flip the direction and pull the desk away on
the second press.

Stage 0 is the state after a chip erase or an 8-s factory-reset (both EEPROM
slots read `0xFFFF`, which `bui_init()` turns into `low = 0`, `high = 0`). The
very first 3-s both-buttons-hold transitions to Stage 1, populating both slots
with the same value. A second save at a different height transitions to Stage 2;
from then on MEM_UP drives to `high` and MEM_DOWN drives to `low`.

`bui_save_pos()` in `src/bekant/bui.c` implements the storage column. The stored
value is the actual save position (no decel offset). The BUI's `bui_set_pos()`
decel-overrun estimator triggers `BCTRL_STOP` just short of the target by an
amount scaled to how far the desk has travelled in the current recall (proxy for
momentum), so the post-`BCTRL_STOP` coast lands precisely on the target —
whether the recall starts from below or from above.

The detailed state-machine transitions are documented in `docs/TRACE.md` and the
disassembly-derived OEM reference is in
`oem_reconstruction/docs/FACTORY_RESET.md`.
