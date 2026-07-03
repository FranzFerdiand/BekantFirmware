/**
 * @file eeprom_defaults.h
 * @brief BEKANT controller — EEPROM helpers and factory-reset.
 *
 * The OEM firmware writes four 16-bit default positions to EEPROM
 * during the factory-reset gesture.  This module exposes:
 *
 *   eeprom_factory_reset()    — overwrite the four OEM defaults
 *   eeprom_load16(addr)       — read a 16-bit value
 *   eeprom_store16(addr, val) — write a 16-bit value
 *
 * The OEM firmware layout (per oem_reconstruction/docs/FACTORY_RESET.md):
 *   EEPROM[0x00] = 0x0021 (physical bottom endstop)
 *   EEPROM[0x02] = 0x00a0 (low memory position)
 *   EEPROM[0x04] = 0x0120 (high memory position)
 *   EEPROM[0x06] = 0x01d2 (physical top endstop)
 *
 * Note that this BUI project's `__eeprom saved` struct also writes
 * to EEPROM[0x00] and EEPROM[0x02].  After factory-reset, the BUI
 * layer overrides those two slots with the OEM endstop values so that
 * MEM_UP/MEM_DOWN move to the physical limits of the desk rather
 * than to the OEM intermediate "memory" positions.
 */
#ifndef EEPROM_DEFAULTS_H
#define EEPROM_DEFAULTS_H

#include <stdint.h>

/** OEM default position values, from FACTORY_RESET.md §"The EEPROM defaults". */
#define OEM_BOTTOM_ENDSTOP   0x0021u
#define OEM_LOW_MEMORY       0x00a0u
#define OEM_HIGH_MEMORY      0x0120u
#define OEM_TOP_ENDSTOP      0x01d2u

/** Write the four OEM default positions to EEPROM (endstops + memory). */
void eeprom_factory_reset(void);

/** Read a 16-bit value from EEPROM. */
uint16_t eeprom_load16(uint8_t addr);

/** Write a 16-bit value to EEPROM. */
void eeprom_store16(uint8_t addr, uint16_t value);

#endif /* EEPROM_DEFAULTS_H */
