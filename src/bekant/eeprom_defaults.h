/**
 * @file eeprom_defaults.h
 * @brief BEKANT controller — EEPROM helpers and factory-reset.
 *
 * The factory-reset gesture clears the saved memory positions by
 * writing 0xFFFF (the unprogrammed-chip value) to all four EEPROM
 * position slots.  On the next boot `bui_init()` reads these as
 * `(0, 0)` in memory (per the "fresh chip" rule in
 * `src/bekant/bui.c::bui_init`), so the next `INPUT_SAVE` populates
 * both slots per the fresh-chip save rule.
 *
 *   eeprom_factory_reset()    — write 0xFFFF to the 4 position slots
 *   eeprom_load16(addr)       — read a 16-bit value
 *   eeprom_store16(addr, val) — write a 16-bit value
 *
 * The OEM firmware (per oem_reconstruction/docs/FACTORY_RESET.md)
 * wrote 0x0021 / 0x00a0 / 0x0120 / 0x01d2 to the same four slots;
 * our buildable firmware uses a simpler "reset to fresh" model.
 */
#ifndef EEPROM_DEFAULTS_H
#define EEPROM_DEFAULTS_H

#include <stdint.h>

/** Write 0xFFFF (unprogrammed) to all 4 EEPROM position slots. */
void eeprom_factory_reset(void);

/** Read a 16-bit value from EEPROM. */
uint16_t eeprom_load16(uint8_t addr);

/** Write a 16-bit value to EEPROM. */
void eeprom_store16(uint8_t addr, uint16_t value);

#endif /* EEPROM_DEFAULTS_H */
