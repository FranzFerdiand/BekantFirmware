# Troubleshooting

- **PICkit not detected.** Check the USB cable. On Linux you may need udev
  rules; Microchip publishes them in
  `/opt/microchip/mplabx/<version>/mplab_ide/docs/` after install.
- **"Device ID mismatch".** The PIC you have is not a PIC16LF1938. Double-check
  the chip marking on the top of the IC.
- **"Programming failed at address 0x0000".** The ICSP cable is reversed or a
  pin is shorted. Inspect the connector and try again.
- **The desk doesn't move but the controller's lights are on.** The bus scan
  failed. Power-cycle the desk and try again.
- **The relay stays engaged at the endstop.** This _may_ be the same bug as
  [ivanwick issue #4](https://github.com/ivanwick/bekantfirmware/issues/4) (we
  have not confirmed). The proposed endstop detector is included in the
  `oem_reconstruction/` folder and is wired into the main build
  (`src/bekant/endstop.c`, registered in `user.c::InitApp()` and ticked from
  `bctrl_timer()`). It has not been verified on a real BEKANT controller. See
  [ENDSTOP_ANALYSIS.md](ENDSTOP_ANALYSIS.md) for the analysis and its caveats.
