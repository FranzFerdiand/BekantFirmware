# The 8-second both-buttons-hold factory reset

The OEM firmware _appears_ to support a **factory-reset** sequence: holding both
buttons for ~8 seconds (best guess from disassembly) clears the saved positions
and rewrites the EEPROM defaults. None of the following has been verified on
hardware — the behaviour is inferred from the disassembly.

> The factory-reset hold time was changed from 10 s to 8 s in the buildable
> firmware (`src/btn/btn.c::FACTORY_HOLD_THRESHOLD = 160`). The
> disassembly-derived "10 s = 200 ticks" inference in this document is
> best-effort and was never observed directly; we keep 8 s in our build because
> it is quicker to use and matches the number we can reproduce reliably from
> disassembly.
>
> The buildable firmware in `src/` also diverges from the OEM in _what the reset
> writes_: this document describes the OEM behaviour of writing the four OEM
> default positions to EEPROM (`0x0021`, `0x00a0`, `0x0120`, `0x01d2`); the
> buildable firmware instead writes `0xFFFF` to all four slots and forces the
> in-memory `low_pos` / `high_pos` to `(0, 0)`, so the next 3-second save
> behaves like a fresh-chip first save (populates both slots). See `README.md`
> §"Save rules" for the user-facing description.

## The gesture

The user holds both `△` and `▽` buttons for 8 seconds. After 8 seconds, the
saved positions are reset to the OEM defaults (`0x0021`, `0x00a0`, `0x0120`,
`0x01d2`) and the controller emits a single "click" of the relay as feedback.
(We believe this is the behaviour; we have not observed it.)

## The state machine (best guess)

We believe the factory-reset path uses the BCTRL state machine at 0x05C0.
Specifically:

- The button handler in `btn/btn.c` appears to increment a counter `0x42` while
  both buttons are held.
- After 8 seconds (160 ticks at 50 ms per tick, the exact threshold is what we
  think we recovered from the disassembly at 0x05DD), the button handler appears
  to write the magic byte `0x55` to RAM byte `0x4c` and set the BCTRL state
  to 1.
- The BCTRL state machine appears to read `0x4c` at 0x05D8:
  - If `0x4c == 0x55`, state := 2 and start a counter.
  - If `0x4c != 0x55`, state := 0 and clear 0x53.
- State 2 (factory-reset armed) at 0x05EE appears to:
  - Clear 0x4d (state := 0).
  - Set bit 0x2B.4 (a "reset done" flag).
  - Read 0x4c and call 0x00D3 (the BCMD dispatcher).
- The BCMD dispatcher at 0x00D3 appears to write 0x49 (BCMD state 2 = init) to
  register 0x161 (bank-2 register 0x61, set up by the preceding `movlb 0x02`),
  which the LIN driver would send on the next bus frame.
- The next state machine tick _seems_ to rewrite the EEPROM defaults via the
  function at 0x0221.

## The button counter (best guess)

The button counter at `0x42` _looks_ like the 8-second timer. From the
disassembly:

- 0x05DD: `decfsz 0x42, 0x0` — decrement 0x42, skip if zero
- 0x05E0: `return` — if not zero, return (wait another tick)
- 0x05E1-0x05E6: if zero, write 0x1a to SFR, clear 0x42, return

So the counter appears to be **decremented** each tick (50 ms), and the magic
byte is processed when it reaches 0. The counter is initialised to 160 (8
seconds × 20 Hz = 160 ticks) by the button handler — at least, that is our
reading.

## The EEPROM defaults

The four default positions we believe the factory-reset writes:

| Address | Value | ivanwick equivalent (if any) | | --------- --------
------------------------------ | | 0x00 | 0x0021 | 0x0636 (sitting) | | 0x02 |
0x00a0 | - | | 0x04 | 0x0120 | - | | 0x06 | 0x01d2 | 0x1600 (standing) |

The ivanwick firmware only stores two positions (sitting and standing), so the
OEM firmware _appears_ to have **two extra positions** stored in EEPROM. We do
not know what they are used for; the ivanwick BUI does not use them.

The ivanwick defaults appear to be deliberately different from the OEM defaults:

- 0x0636 is described as a typical sitting height (around 70 cm).
- 0x1600 is described as a typical standing height (around 110 cm).
- 0x0021 / 0x01d2 are the OEM defaults, which look like they are at the **end
  stops** of the desk's range.

ivanwick's defaults appear to assume the user has already moved the desk to the
desired positions and saved them with the 3-second both-buttons-hold gesture.
The OEM defaults _appear_ to put the desk at the physical endstops, which would
be the correct behaviour for a factory-reset.

## What the C reconstruction does

The `bekant/eeprom_defaults.c` file has the `eeprom_factory_reset()` function
that performs what we think is the actual EEPROM rewrite. It is called from the
BUI layer when the factory-reset path is detected (in the OEM firmware, we
believe this is the state machine transition from state 1 to state 2 in the
0x05C0 dispatcher).

The `btn/btn.c` (in this folder) is responsible for detecting the 8-second
both-buttons-hold gesture and writing the magic byte `0x55` to RAM. The C code
does this by:

1. Counting ticks while both buttons are held.
2. When the count reaches 160 (8 seconds), write 0x55 to `bctrl_factory_magic`
   and set the BCTRL state to 1.
3. The BCTRL state machine then handles the actual reset.

## What's not implemented in `oem_reconstruction/`

The ivanwick-derived state machine in this folder's `btn/btn.c` and
`bekant/bui.c` are placeholders — they recognise the 3-second save gesture but
punt the 8-second factory-reset sequence to a "higher level" that is not present
here. No file in `oem_reconstruction/` is compiled by the project Makefile
(which builds only `src/`); the files here serve as a disassembler-derived
reference, not as buildable firmware.

## Buildable implementation in `src/`

The buildable firmware in `../src/` does implement the factory-reset, using a
**stacked-threshold** button state machine rather than a BCTRL-state-machine
dispatcher:

| File | Role | | -----------------------------------
--------------------------------------------------------------------------------
| | `src/btn/btn.c` | Stacked thresholds: 60 ticks → `INPUT_SAVE`, 160 ticks →
`INPUT_FACTORY_RESET` | | `src/btn/btn.h` | `INPUT_FACTORY_RESET` enum value | |
`src/bekant/bui.c` | `bui_input()` latches the gesture; `bui_service()` (main
loop) → `bui_factory_reset()` | | `src/bekant/eeprom_defaults.{c,h}` |
`eeprom_factory_reset()` (writes 4 OEM positions to EEPROM) | |
`src/bekant/eeprom_defaults.{c,h}` | `eeprom_load16` / `eeprom_store16`
raw-register helpers | | `Makefile` | Adds `bekant/eeprom_defaults.c` to `SRCS`
|

The stacked-threshold approach intentionally differs from the
disassembly-derived `BCTRL_STATE_FACTORY_RESET_*` enumeration above. It is
simpler, fits the existing ivanwick + endstop-fixer architecture, and gives the
user distinct SAVE / FACTORY_RESET events on the wire (BUI consumes each event
separately).

Behaviour summary, derived from the state machine (`src/btn/btn.c:128-168`):

| Hold duration | Gesture fired | Notes | | -------------------------
------------------------------ ----------------------------------------- | |
0-59 ticks (< 3 s) | none | Counter increments but does not fire | | 60 ticks (=
3 s) | `INPUT_SAVE` | Save current position to EEPROM | | 61-159 ticks (3-8 s) |
`INPUT_SAVE` (already fired) | Counter continues toward factory | | 160 ticks (=
8 s) | `INPUT_FACTORY_RESET` | Writes 4 OEM defaults to EEPROM | | > 160 ticks |
(no further events) | Stays in `INPUT_FACTORY_RESET` state | | release |
`INPUT_IDLE` | Counter resets for next gesture |

A user who holds both buttons for the full 8 s will see **two events** —
`INPUT_SAVE` at 3 s (a single relay click) and `INPUT_FACTORY_RESET` at 8 s
(single click after EEPROM is rewritten to OEM defaults). The second event's
EEPROM write supersedes the first.

## See also

- `bekant/eeprom_defaults.c::bui_factory_reset()` — the reset function
  (best-effort reconstruction)
- `bekant/bctrl.c::bctrl_state_machine()` — the state machine that handles the
  reset
- `docs/STATE_MACHINE.md` — the full state diagram
