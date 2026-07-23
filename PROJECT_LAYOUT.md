# Project layout

```
BekantFirmware/
├── INSTALL.md
├── PROJECT_LAYOUT.md                    ← this file
├── README.md
├── docs/
│   ├── ABOUT.md
│   ├── COMPARISON.md
│   ├── DELIVERABLE.md
│   ├── diagram_white_bg.png
│   ├── ENDSTOP_ANALYSIS.md
│   ├── TESTING.md
│   ├── TRACE.md
│   └── TROUBLESHOOTING.md
├── oem_reconstruction/                  ← reverse-engineered reference impl
│   ├── README.md
│   ├── main.c / system.c / user.c / interrupts.c / configuration_bits.c
│   ├── btn/ lin/ bekant/
│   ├── bekant/endstop.c                 ← recovered endstop detector
│   ├── bekant/bcmd_table.c / bcmd_table.h / oem_tables.c
│   ├── docs/                            ← STATE_MACHINE / ENCODER_TABLE / FACTORY_RESET
│   └── disassembly/                     ← orginafirm.hex + .asm dumps
└── src/
    ├── main.c
    ├── configuration_bits.c
    ├── system.c / system.h
    ├── user.c / user.h
    ├── interrupts.c                     ← void __interrupt() isr(void) (the ISR fix)
    ├── Makefile
    ├── btn/btn.h                        ← INPUT_t enum, gesture documentation
    ├── btn/btn.c                        ← unified state machine; stacked 3 s/8 s thresholds
    ├── lin/lin_d.c / lin_d.h            ← LIN driver
    ├── bekant/bscan.c / bscan.h         ← startup bus scan
    ├── bekant/bctrl.c / bctrl.h         ← LIN control plane (uses ivanwick's BCMD values)
    ├── bekant/bui.c / bui.h             ← upper-level UI (handles INPUT_SAVE + INPUT_FACTORY_RESET)
    ├── bekant/eeprom_defaults.{c,h}     ← raw-reg EEPROM helpers, eeprom_factory_reset()
    ├── bekant/endstop.c / endstop.h     ← recovered endstop detector, wired in
    └── test_sfr_stubs.h / .c            ← syntax-only shim (real build uses src/ directly)
```
