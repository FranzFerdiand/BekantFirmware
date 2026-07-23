/**
 * @file eeprom_defaults.c
 * @brief BEKANT controller — EEPROM helpers and factory-reset.
 *
 * The factory-reset path is wired up from the button handler:
 * holding both buttons for 8 seconds fires INPUT_FACTORY_RESET,
 * which calls bui_factory_reset(), which calls eeprom_factory_reset().
 *
 * The four OEM default positions written here are lifted from
 * oem_reconstruction/docs/FACTORY_RESET.md and oem_reconstruction/
 * bekant/eeprom_defaults.c.  They are written at EEPROM offsets
 * 0x00, 0x02, 0x04, 0x06 — the same layout the OEM firmware uses.
 *
 * The helpers here use raw register access (not __eeprom-qualified
 * variables) so that bui_init() can read EEPROM and bui_service()
 * can write EEPROM (both from mainline) without tripping XC8
 * error 1401 about __eeprom access from conflicting contexts.
 *
 * Writes run from mainline (never from an ISR — see bui.c): each byte
 * takes ~5 ms, so the busy-wait feeds the watchdog (CLRWDT) and the
 * EECON2 unlock sequence is guarded against interrupts per the
 * datasheet-recommended write procedure.
 */

#include "eeprom_defaults.h"

#include <xc.h>
#include <stdint.h>

static void eeprom_write_byte(uint8_t addr, uint8_t value) {
    EEADR = addr;
    EEDATA = value;
    EECON1bits.EEPGD = 0;     /* access data EEPROM, not program memory */
    EECON1bits.CFGS  = 0;     /* access EEPROM, not config words */
    EECON1bits.WREN  = 1;     /* allow writes */
    INTCONbits.GIE   = 0;     /* unlock sequence must not be interrupted */
    EECON2 = 0x55;
    EECON2 = 0xaa;
    EECON1bits.WR    = 1;     /* start write */
    INTCONbits.GIE   = 1;
    while (EECON1bits.WR) {
        CLRWDT();             /* write takes ~5 ms; keep the WDT happy */
    }
    EECON1bits.WREN  = 0;     /* disallow writes (anti-miswrite) */
}

/* Write a 16-bit value to EEPROM at the given address (low byte at
 * addr, high byte at addr+1, matching what XC8 generates for
 * little-endian __eeprom int16_t variables). */
static void eeprom_write_word(uint8_t addr, uint16_t value) {
    eeprom_write_byte(addr, (uint8_t)(value & 0xff));
    eeprom_write_byte(addr + 1, (uint8_t)((value >> 8) & 0xff));
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
    /* Clear all 4 EEPROM position slots to 0xFFFF (the unprogrammed
     * value), so a freshly-reset chip looks identical to a newly-
     * erased chip.  bui_init() maps 0xFFFF reads to (0, 0) in memory,
     * so the next INPUT_SAVE populates both slots per the fresh-chip
     * rule in src/bekant/bui.c::bui_save_pos. */
    eeprom_write_word(0x00, 0xFFFF);
    eeprom_write_word(0x02, 0xFFFF);
    eeprom_write_word(0x04, 0xFFFF);
    eeprom_write_word(0x06, 0xFFFF);
}

uint16_t eeprom_load16(uint8_t addr) {
    return eeprom_read_word(addr);
}

void eeprom_store16(uint8_t addr, uint16_t value) {
    eeprom_write_word(addr, value);
}
