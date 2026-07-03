/**
 * @file eeprom_defaults.c
 * @brief BEKANT controller — EEPROM helpers and factory-reset.
 *
 * The factory-reset path is wired up from the button handler:
 * holding both buttons for 10 seconds fires INPUT_FACTORY_RESET,
 * which calls bui_factory_reset(), which calls eeprom_factory_reset().
 *
 * The four OEM default positions written here are lifted from
 * oem_reconstruction/docs/FACTORY_RESET.md and oem_reconstruction/
 * bekant/eeprom_defaults.c.  They are written at EEPROM offsets
 * 0x00, 0x02, 0x04, 0x06 — the same layout the OEM firmware uses.
 *
 * The helpers here use raw register access (not __eeprom-qualified
 * variables) so that:
 *   - bui_init() can read EEPROM from mainline, and
 *   - bui_save_pos() / bui_factory_reset() can write EEPROM from
 *     the Timer2 ISR call chain (btn_timer → bui_input → …).
 * XC8 forbids mixing those two contexts against __eeprom variables
 * (error 1401); raw register writes are unrestricted.
 */

#include "eeprom_defaults.h"

#include <xc.h>
#include <stdint.h>

/* Write a 16-bit value to EEPROM at the given address (low byte at
 * addr, high byte at addr+1, matching what XC8 generates for
 * little-endian __eeprom int16_t variables). */
static void eeprom_write_word(uint8_t addr, uint16_t value) {
    EEADR = addr;
    EEDATA = (uint8_t)(value & 0xff);
    EECON1bits.EEPGD = 0;     /* access data EEPROM, not program memory */
    EECON1bits.CFGS  = 0;     /* access EEPROM, not config words */
    EECON1bits.WREN  = 1;     /* allow writes */
    EECON2 = 0x55;
    EECON2 = 0xaa;
    EECON1bits.WR    = 1;     /* start write */
    while (EECON1bits.WR) ;   /* wait for hardware completion */
    EECON1bits.WREN  = 0;     /* disallow writes (anti-miswrite) */

    EEADR = addr + 1;
    EEDATA = (uint8_t)((value >> 8) & 0xff);
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS  = 0;
    EECON1bits.WREN  = 1;
    EECON2 = 0x55;
    EECON2 = 0xaa;
    EECON1bits.WR    = 1;
    while (EECON1bits.WR) ;
    EECON1bits.WREN  = 0;
}

static uint16_t eeprom_read_word(uint8_t addr) {
    EEADR = addr;
    EECON1bits.CFGS  = 0;
    EECON1bits.EEPGD = 0;
    EECON1bits.RD    = 1;     /* start read */
    uint8_t lo = EEDATA;
    EEADR = addr + 1;
    EECON1bits.RD    = 1;
    uint8_t hi = EEDATA;
    return ((uint16_t)hi << 8) | lo;
}

void eeprom_factory_reset(void) {
    eeprom_write_word(0x00, OEM_BOTTOM_ENDSTOP);
    eeprom_write_word(0x02, OEM_LOW_MEMORY);
    eeprom_write_word(0x04, OEM_HIGH_MEMORY);
    eeprom_write_word(0x06, OEM_TOP_ENDSTOP);
}

uint16_t eeprom_load16(uint8_t addr) {
    return eeprom_read_word(addr);
}

void eeprom_store16(uint8_t addr, uint16_t value) {
    eeprom_write_word(addr, value);
}
