# Using the BEKANT controller

The IKEA BEKANT desk has two buttons on the front of the controller
box: **up** (`△`) and **down** (`▽`). This firmware turns those two
buttons into a full position-memory controller — sit, stand, save.

## Basic motion

|  To do this           |  Press                        |
| ---------------------  ----------------------------- |
|  Move the desk up     |  hold `△`                     |
|  Move the desk down   |  hold `▽`                     |

Release the button to stop. The desk stops as soon as you let go.

## Memory positions

You can save one sitting position and one standing position. Once
saved, you can move to either with a single button gesture.

### Going to the upper (standing) position

Pick whichever feels natural — both work:

- **Combo-press:** hold `△` and tap `▽` while still holding `△`
- **Double-tap:** tap `△` twice within half a second

### Going to the lower (sitting) position

- **Combo-press:** hold `▽` and tap `△` while still holding `▽`
- **Double-tap:** tap `▽` twice within half a second

The desk will move on its own until it reaches the saved position.
Press any button to cancel mid-movement.

### Saving the current position

To overwrite one of the memory slots with the desk's current height:

1. Move the desk to the height you want to save.
2. Press and hold **both** buttons for **3 seconds**.
3. The legs will click once when the position is saved. You can
   release the buttons now, **or keep holding** to escalate to
   a factory reset (see next section).

The save gesture overwrites whichever memory slot (sitting or
standing) is **closer** to the current position. So if the desk is
near the bottom, the lower memory is updated; if it's near the top,
the upper memory is updated. You don't have to tell the firmware
which one you're saving.

### Factory reset (10-second both-buttons-hold)

If you keep holding both buttons past the 3-s mark, the gesture
**escalates** at the 10-second mark and triggers a factory reset:

1. Continue holding both buttons past 3 s. The first click at 3 s
   was the **save** feedback above.
2. At **10 seconds** of holding both buttons, the legs click a
   second time and the saved memory positions are rewritten to
   the OEM endstops.
3. Release both buttons. The desk is now configured with:
   - **Lower position:** physical bottom endstop (`0x0021`)
   - **Upper position:** physical top endstop (`0x01d2`)

This is the gesture the original IKEA firmware appears to use,
per `oem_reconstruction/docs/FACTORY_RESET.md`. It restores the
controller to "as it left the factory" memory positions without
affecting any other state.

## First-time setup

The firmware ships with sensible defaults:

- **Lower position (sitting):** about 70 cm
- **Upper position (standing):** about 110 cm

To change them, just move the desk to the new height and use the
**save** gesture above. The new value is stored permanently in the
controller's EEPROM and will survive power cycles.

To restore the factory defaults for the memory positions, use the
**factory reset** gesture above.

## Edge cases

|  Situation                                                    |  What happens                                         |
| -------------------------------------------------------------  ----------------------------------------------------- |
|  Press `△` then press `△` again after 1 second                |  Two separate up movements, no memory move            |
|  Hold both buttons for less than 3 seconds and release        |  Nothing — you have to hold the full 3 s              |
|  Hold both buttons past 3 s but less than 10 s, then release  |  One `INPUT_SAVE` event at 3 s, then idle             |
|  Hold both buttons for the full 10 s, then release            |  `INPUT_SAVE` at 3 s + `INPUT_FACTORY_RESET` at 10 s  |
|  Press `△` once while already moving up                       |  No effect — keep holding                             |
|  Power cut during a memory move                               |  Desk stops; press a button to restart                |
|  Press a button during a memory move                          |  Memory move cancels, you can drive manually          |

## Test scenarios

These are the hold-duration scenarios the button state machine
in `src/btn/btn.c` is designed to handle. Tick rate is 50 ms
(20 Hz) — see `btn/btn.c::btn_init()` for the Timer2 setup.

|  Hold duration            |  State transitions                                         |  Events fired                                       |  Final EEPROM (low, high)       |
| -------------------------  ----------------------------------------------------------  ---------------------------------------------------  ------------------------------- |
|  release at 50 ticks      |  IDLE (count 1-49) → IDLE                                  |  (none)                                             |  unchanged                      |
|  release at 60 ticks      |  IDLE (1-59) → IDLE → SAVE (60th tick) → IDLE              |  `INPUT_SAVE`                                       |  current position               |
|  release at 100 ticks     |  IDLE (1-59) → IDLE → SAVE → SAVE → IDLE                   |  `INPUT_SAVE`                                       |  current position               |
|  release at 199 ticks     |  IDLE → SAVE → SAVE → IDLE                                 |  `INPUT_SAVE`                                       |  current position               |
|  release at 200 ticks     |  IDLE → SAVE (60) → SAVE → FACTORY_RESET (200) → IDLE      |  `INPUT_SAVE` + `INPUT_FACTORY_RESET`               |  OEM defaults (0x0021, 0x01d2)  |
|  release at 250 ticks     |  IDLE → SAVE → FACTORY_RESET → IDLE                        |  `INPUT_SAVE` + `INPUT_FACTORY_RESET`               |  OEM defaults                   |
|  release immediately      |  IDLE                                                      |  (none)                                             |  unchanged                      |
|  release twice quickly    |  press-release-press-release with total held ticks < 60    |  (none — counter accumulates but never crosses 60)  |  unchanged                      |

Notes on the table:

- **`INPUT_SAVE` (3 s mark):** The desk's relay clicks once. The
  position closest to the current height (upper or lower memory slot)
  is overwritten with the current encoder value.
- **`INPUT_FACTORY_RESET` (10 s mark):** The desk's relay clicks a
  second time. The four OEM default positions are written to EEPROM:
  `0x0021` (low), `0x00a0`, `0x0120`, and `0x01d2` (high). The BUI
  then overwrites the two BUI slots at `0x00` and `0x02` with the
  endstop values, so MEM_UP / MEM_DOWN go to the physical limits.
- **Why two events on a 10-s hold:** The state machine uses
  **stacked thresholds** rather than "either/or" — the SAVE event
  fires at the 60-tick mark and the FACTORY_RESET event fires at
  the 200-tick mark. This gives the user two distinct audible
  confirmation clicks (3 s then 10 s) which match what the original
  IKEA firmware appears to do (`oem_reconstruction/docs/FACTORY_RESET.md:14-15`).
- **Counter persistence on release:** if the user holds to, say, 50
  ticks, releases, then re-presses, the counter resumes from 50 (it
  is not zeroed on release). This is pre-existing ivanwick
  behaviour that we did not change.
- **A full 10-s hold is two events but the final state is the OEM
  defaults**, since `INPUT_FACTORY_RESET` writes after `INPUT_SAVE`.

The detailed state-machine transitions are documented in
`docs/TRACE.md` and the disassembly-derived OEM reference is in
`oem_reconstruction/docs/FACTORY_RESET.md`.

## Why two gestures?

The two encodings (combo-press and double-tap) are both supported on
purpose:

- **Combo-press** is the original ivanwick firmware style. It's
  hard to trigger accidentally because you have to deliberately hold
  one button and tap the other.
- **Double-tap** is the DieSteinhose fork style. It's what people
  learn from elevator buttons — natural and one-handed.

If you're new to the controller, try both and use whichever feels
better. The firmware doesn't care which you pick.

## Safety

- Keep the desk's path clear during all movement.
- Stay near the controller while a memory move is in progress, so
  you can cancel by pressing any button.
- The firmware *is intended to* drop the relay when the desk
  reaches the physical endstop, so the motor cannot stall
  indefinitely. (This is implemented in `src/bekant/endstop.c`
  via a separate endstop-detector module; we believe this
  fixes the bug described in
  [ivanwick issue #4](https://github.com/ivanwick/bekantfirmware/issues/4),
  but the endstop behaviour has not been verified on a real
  BEKANT controller.)
- If something goes wrong, cutting power stops the desk
  immediately.

## Disclaimer

Use at your own risk. Position memory is convenient but the original
IKEA firmware omits it deliberately so that a human is always in
the control loop during movement. This firmware keeps the same
spirit: any button press cancels an automatic move.
