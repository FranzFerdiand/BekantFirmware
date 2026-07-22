/**
 * BEKANT UI
 */

#include "bui.h"
#include "bctrl.h"
#include "eeprom_defaults.h"
#include <stdlib.h> // abs()
#include <stdbool.h>

/* For syntax-only checks without the real XC8 PIC SFR headers,
 * BEKANT_TEST_SYNTAX pulls in a stub. */
#ifdef BEKANT_TEST_SYNTAX
#include "test_sfr_stubs.h"
#endif

typedef enum {
    BUI_STOP,
    BUI_UP,
    BUI_MEM_UP,
    BUI_DOWN,
    BUI_MEM_DOWN
} BUI_state_t;

BUI_state_t bui_state = BUI_STOP;
int16_t high_pos;
int16_t low_pos;
int16_t cur_pos; // accessed by bui_input() and bui_set_pos()

/* Memory positions are stored in EEPROM via the eeprom_defaults.c
 * helpers (raw register writes) rather than via __eeprom-qualified
 * variables, so XC8 does not consider mainline reads (bui_init) and
 * writes (bui_service) an interrupt/mainline conflict (error 1401).
 *
 * EEPROM writes are SLOW (~5 ms per byte) and must not run inside the
 * Timer2 ISR: a factory-reset writes 16 bytes (~80 ms), which would
 * block the Timer4 LIN scheduler and trip the watchdog.  Therefore
 * bui_input() (ISR context) only records a pending request and
 * bui_service() — called from the main loop — performs the writes.
 *
 * Address map (matches the OEM 4-position layout in
 * oem_reconstruction/docs/FACTORY_RESET.md):
 *   EEPROM[0x00..0x01] = low_pos  (also OEM_BOTTOM_ENDSTOP slot)
 *   EEPROM[0x02..0x03] = high_pos (overridden OEM_LOW_MEMORY slot;
 *                                    BUI wants the top endstop here)
 *   EEPROM[0x04..0x05] = OEM_HIGH_MEMORY (not used by BUI directly)
 *   EEPROM[0x06..0x07] = OEM_TOP_ENDSTOP (not used by BUI directly)
 */
#define EEPROM_ADDR_LOW_POS  0x00u
#define EEPROM_ADDR_HIGH_POS 0x02u

void bui_save_pos(int16_t save_pos);
void bui_factory_reset(void);

/* Deferred EEPROM requests, set by bui_input() (ISR) and serviced by
 * bui_service() (mainline).  -1 / false means "nothing pending". */
static volatile int16_t pending_save_pos = -1;
static volatile bool pending_factory_reset = false;

/**
 * Initialize from stored position values.
 *
 * Called from mainline (main.c calls InitApp which calls bui_init).
 * EEPROM reads from raw registers are NOT __eeprom-qualified, so
 * XC8 does not consider this an interrupt/mainline conflict.
 */
void bui_init(void) {
    low_pos  = (int16_t)eeprom_load16(EEPROM_ADDR_LOW_POS);
    high_pos = (int16_t)eeprom_load16(EEPROM_ADDR_HIGH_POS);
}

/**
 * 
 * @param input Input gesture
 */
void bui_input(INPUT_t input) {
    switch (bui_state) {
        case BUI_STOP:
            if (input == INPUT_UP) {
                bui_state = BUI_UP;
                bctrl_set_target(BCTRL_UP);
            } else if (input == INPUT_DOWN) {
                bui_state = BUI_DOWN;
                bctrl_set_target(BCTRL_DOWN);
            } else if (input == INPUT_MEM_UP && cur_pos < high_pos) {
                bui_state = BUI_MEM_UP;
                bctrl_set_target(BCTRL_UP);
            } else if (input == INPUT_MEM_DOWN && cur_pos > low_pos) {
                bui_state = BUI_MEM_DOWN;
                bctrl_set_target(BCTRL_DOWN);
            } else if (input == INPUT_SAVE) {
                /* Defer the EEPROM write to bui_service() (mainline). */
                pending_save_pos = cur_pos;
            } else if (input == INPUT_FACTORY_RESET) {
                pending_factory_reset = true;
            }
            break;

        case BUI_UP:
            if (input == INPUT_MEM_UP && cur_pos < high_pos) {
                bui_state = BUI_MEM_UP;
            } else if (input == INPUT_IDLE) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            } else if (input == INPUT_DOWN) {
                bui_state = BUI_DOWN;
                bctrl_set_target(BCTRL_DOWN);
            }
            break;

        case BUI_DOWN:
            if (input == INPUT_MEM_DOWN && cur_pos > low_pos) {
                bui_state = BUI_MEM_DOWN;
            } else if (input == INPUT_IDLE) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            } else if (input == INPUT_UP) {
                bui_state = BUI_UP;
                bctrl_set_target(BCTRL_UP);
            }
            break;

        case BUI_MEM_UP:
            if (input == INPUT_UP) {
                bui_state = BUI_UP;
            } else if (input == INPUT_DOWN) {
                bui_state = BUI_DOWN;
                bctrl_set_target(BCTRL_DOWN); // switch directions
            }
            break;

        case BUI_MEM_DOWN:
            if (input == INPUT_UP) {
                bui_state = BUI_UP;
                bctrl_set_target(BCTRL_UP); // switch directions
            } else if (input == INPUT_DOWN) {
                bui_state = BUI_DOWN;
            }
            break;
    }
}

/**
 * Update this module's copy of the current position.
 * Expect to be called by BEKANT Control module after the LIN bus transaction
 * is completed and position is reported.
 * 
 * @param pos Current position
 */
void bui_set_pos(int16_t pos) {
    cur_pos = pos;
    switch (bui_state) {
        case BUI_MEM_UP:
            if (cur_pos >= high_pos) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            }
            break;
        case BUI_MEM_DOWN:
            if (cur_pos <= low_pos) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            }
            break;
        default:
            break;
    }
}

/**
 * Service deferred EEPROM requests from the main loop.
 *
 * bui_input() runs in Timer2 ISR context and only latches the request;
 * the actual EEPROM writes happen here so the LIN scheduler (Timer4
 * ISR) keeps running during the ~5 ms/byte writes.  A factory-reset
 * supersedes a pending save.
 */
void bui_service(void) {
    if (pending_factory_reset) {
        pending_factory_reset = false;
        pending_save_pos = -1;
        bui_factory_reset();
    } else if (pending_save_pos >= 0) {
        int16_t save_pos = pending_save_pos;
        pending_save_pos = -1;
        bui_save_pos(save_pos);
    }
}

/**
 * Store the given save_pos encoder value into persistent memory.
 * Overwrite stored low_pos or high_pos, whichever is closer to save_pos.
 * 
 * If the given save_pos looks like an encoder error state (negative),
 * then don't save.
 * 
 * @param save_pos new encoder value to store into persistent memory
 */
void bui_save_pos(int16_t save_pos) {
    if (save_pos < 0) {
        // Encoder value indicates error state
        return;
    }

    int16_t diff_high = abs(high_pos - save_pos);
    int16_t diff_low = abs(low_pos - save_pos);

    // Next time during a MEM move, BUI will send the STOP signal to BCTRL
    // before it reaches the target position, to allow for deceleration.
    if (diff_low < diff_high) {
        low_pos = save_pos + BCTRL_DECEL_MARGIN;
        eeprom_store16(EEPROM_ADDR_LOW_POS, (uint16_t)low_pos);
    } else {
        high_pos = save_pos - BCTRL_DECEL_MARGIN;
        eeprom_store16(EEPROM_ADDR_HIGH_POS, (uint16_t)high_pos);
    }

    // Click feedback
    bctrl_set_target(BCTRL_CLICK);
}

/**
 * Factory-reset handler.
 *
 * Requested by btn.c after the user holds both buttons for 10 seconds
 * (the INPUT_FACTORY_RESET event, latched by bui_input) and executed
 * from mainline by bui_service().  Resets the saved memory positions
 * to the OEM endstops and writes the OEM 4-position layout to EEPROM.
 *
 * Sequence:
 *   1. eeprom_factory_reset() — writes 4 OEM defaults to EEPROM[0x00..0x07].
 *      This populates the two intermediate OEM memory positions
 *      (0x00a0, 0x0120) at slots 0x02 and 0x04 which the BUI layer
 *      does not use directly.
 *   2. Override the two BUI slots at 0x00 and 0x02 with the OEM endstops,
 *      so MEM_UP / MEM_DOWN move to the physical limits of the desk.
 *   3. Update in-memory low_pos / high_pos.
 *   4. Single relay click as audible feedback (same as bui_save_pos).
 *
 * See oem_reconstruction/docs/FACTORY_RESET.md for OEM behaviour.
 */
void bui_factory_reset(void) {
    eeprom_factory_reset();

    /* Override the BUI's high_pos slot with the OEM top-endstop value
     * (eeprom_factory_reset wrote OEM_LOW_MEMORY=0x00a0 there, which
     * the BUI doesn't want — MEM_UP should go all the way up). */
    eeprom_store16(EEPROM_ADDR_LOW_POS,  OEM_BOTTOM_ENDSTOP);
    eeprom_store16(EEPROM_ADDR_HIGH_POS, OEM_TOP_ENDSTOP);

    low_pos  = (int16_t)eeprom_load16(EEPROM_ADDR_LOW_POS);
    high_pos = (int16_t)eeprom_load16(EEPROM_ADDR_HIGH_POS);

    bctrl_set_target(BCTRL_CLICK);
}
