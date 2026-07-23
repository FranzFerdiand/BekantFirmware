# About this fork

Analysis of the original OEM firmware (disassembly and rebuild in C) was done
using the MiniMax-M3 LLM, followed by a cleanup session using Claude Opus 4.8.

First live tests still had some major issues, but were resolved using the Kimi
K3 LLM. After this, manual operation, storing (to be improved) and positioning
(both by double-clicking △△ or ▽▽ and sequence-clicking △+▽ or ▽+△) do work.

The original release at
[ivanwick/bekantfirmware](https://github.com/ivanwick/bekantfirmware) adds
position memory to the OEM controller. The
[DieSteinhose/bekantfirmware](https://github.com/DieSteinhose/bekantfirmware)
fork changes the gesture you use to invoke a memory position.

This project:

1. **Supports both gesture styles at the same time.** You can use ivanwick's
   "combo-press" (hold one button, tap the other) or DieSteinhose's "double-
   tap". The unified state machine in `btn/btn.c` handles both.
2. **Implements an 8-second factory-reset** by holding both buttons. Distinct
   from the existing 3-second save gesture (which saves the current position)
   the 8-second hold clears the saved memory positions so the next 3-second save
   behaves like a first save on a fresh chip (populates both slots). The two
   gestures share one counter using a _stacked-threshold_ state machine, so an
   8-second hold fires both events (SAVE at 3 s, FACTORY_RESET at 8 s).
3. **Includes a best-effort C reconstruction** of the original IKEA firmware in
   `oem_reconstruction/`, for future reference and as a _tentative_ attempt at
   the endstop behaviour described in
   [ivanwick issue #4](https://github.com/ivanwick/bekantfirmware/issues/4).

The rest of the source is mostly identical to ivanwick's original project, with
these additions / fixes:

- **`btn/`** rewritten to support both gesture styles at the same time, plus the
  stacked 3 s/8 s thresholds for save / factory-reset.
- **`bekant/bui.c`** now responds to `INPUT_FACTORY_RESET` by calling
  `bui_factory_reset()` which clears the saved memory positions to fresh-chip
  state (in-memory `(0, 0)` plus `0xFFFF` in all four EEPROM slots) so the next
  3-second save populates both slots.
- **`bekant/eeprom_defaults.{c,h}`** provides raw-register EEPROM read/write
  helpers and `eeprom_factory_reset()`. EEPROM writes run from mainline only:
  `bui_input()` (Timer2 ISR) just latches the request and `bui_service()` —
  called from the main loop — performs the writes (with `CLRWDT()` in the ~5
  ms/byte busy-wait), so the Timer4 LIN scheduler keeps running during save /
  factory-reset.
- **`bekant/bctrl.{c,h}`** uses ivanwick's BCMD values (0x86 up / 0x85 down /
  0xfc stop / …), which are proven on hardware — the desk moves in both
  directions while a button is held. The OEM bytes recovered from the
  disassembly (0xca/0x0d/…) turned out to belong to the encoder/endstop-check
  layer, not the direct motion commands. See
  [ENDSTOP_ANALYSIS.md](ENDSTOP_ANALYSIS.md).
- **`bekant/endstop.{c,h}`** is the _proposed_ endstop / over-travel detector (a
  _tentative_ fix for issue #4); it is registered from `user.c::InitApp()` and
  ticked once per scheduler slot from `bctrl_timer()`. Not yet verified.
- **`interrupts.c`** declares the ISR with the `__interrupt()` qualifier —
  without it, XC8 free mode appears to dead-strip the function and the interrupt
  vector would stay empty.
