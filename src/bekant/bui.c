/**
 * BEKANT UI
 */

#include "bui.h"
#include "bctrl.h"
#include "eeprom_defaults.h"
#include "radiator_config.h"
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
 * Timer2 ISR: a factory-reset writes 8 bytes (~40 ms), which would
 * block the Timer4 LIN scheduler and trip the watchdog.  Therefore
 * bui_input() (ISR context) only records a pending request and
 * bui_service() — called from the main loop — performs the writes.
 *
 * Address map (matches the OEM firmware's 4-position layout in
 * oem_reconstruction/docs/FACTORY_RESET.md, even though our buildable
 * firmware only uses the first two slots):
 *   EEPROM[0x00..0x01] = low_pos  (OEM "bottom endstop" slot)
 *   EEPROM[0x02..0x03] = high_pos (OEM "low memory" slot)
 *   EEPROM[0x04..0x05] = (OEM "high memory"; not used by BUI)
 *   EEPROM[0x06..0x07] = (OEM "top endstop"; not used by BUI)
 *
 * On factory-reset, all four slots are written with 0xFFFF so the
 * firmware starts from a fresh-chip state on the next boot, and the
 * in-memory low_pos / high_pos are forced to (0, 0) immediately so
 * the next INPUT_SAVE populates both slots per the fresh-chip rule.
 */
#define EEPROM_ADDR_LOW_POS  0x00u
#define EEPROM_ADDR_HIGH_POS 0x02u

void bui_save_pos(int16_t save_pos);
void bui_factory_reset(void);

/* Deferred EEPROM requests, set by bui_input() (ISR) and serviced by
 * bui_service() (mainline).  -1 / false means "nothing pending". */
static volatile int16_t pending_save_pos = -1;
static volatile bool pending_factory_reset = false;

/* Stop position for an in-progress MEM_* recall.  Set by recall_memory()
 * (called from bui_input() in the Timer2 ISR) and read by bui_set_pos()
 * (called from bctrl_timer in the Timer4 ISR).  When cur_pos is on
 * the "wrong" side of the requested target, recall_memory() inverts
 * the driving direction so the desk converges on the target from
 * above instead of being unable to fire; bui_set_pos() still stops
 * when cur_pos reaches mem_target.
 *
 * `mem_entry_pos` is `cur_pos` at the moment the MEM_* recall started;
 * the overrun estimator (overrun_for, below) uses how far the desk has
 * travelled in this recall to scale the BUI stop trigger so the desk
 * lands precisely on the target — even when the recall starts near the
 * target with very little momentum.
 *
 * Plain (non-volatile) is safe: the two ISRs share the single PIC16
 * interrupt vector and never preempt each other. */
static int16_t mem_target;
static int16_t mem_entry_pos;

/* Estimate the coast distance the desk will travel after BCTRL_STOP
 * fires.  At cruising speed the desk coasts BCTRL_DECEL_MARGIN encoder
 * units; while still accelerating from rest it coasts roughly
 * proportionally to the distance already travelled.  The cap matches
 * the BCTRL_DECEL_MARGIN value (0x6c) the ivanwick baseline calibrated
 * for this hardware.  Negative `moved` is clamped to 0 (defensive:
 * the desk can briefly move in the wrong direction before settling). */
static int16_t overrun_for(int16_t moved) {
    if (moved < 0) return 0;
    if (moved > BCTRL_DECEL_MARGIN) return BCTRL_DECEL_MARGIN;
    return moved;
}

/* No-op deadband for memory recalls: if cur_pos is within this many
 * encoder units of the requested target, the recall is a no-op.
 *
 * Without this, a recall that started slightly past the target on
 * the previous press would now reverse direction (because cur_pos
 * > target) and the desk would drift *away* from target on the
 * second press.  The deadband makes "the desk is already close
 * enough — don't try to micro-correct" the behaviour the user
 * expects from a second press of the same button.
 *
 * 0x10 is ~3% of the BEKANT desk's full encoder range and is well
 * within mechanical precision for either direction. */
#define TARGET_DEADBAND 0x10

/* Start a memory recall moving toward `target`.  The desk converges
 * on `target` from either side: drives UP if cur_pos < target,
 * drives DOWN if cur_pos > target, no movement if already within
 * TARGET_DEADBAND of the target or if no memory is configured. */
static void recall_memory(int16_t target) {
    if (low_pos == 0 && high_pos == 0) {
        return;
    }

    // Hard lower bound (radiator collision protection)
    if (target < RADIATOR_MIN_POS) {
        target = RADIATOR_MIN_POS;
    }

    int16_t diff = cur_pos - target;
    if (diff >= -TARGET_DEADBAND && diff <= TARGET_DEADBAND) {
        /* Already at the target (within deadband).  Don't update
         * `mem_target` / `mem_entry_pos` so a subsequent press of
         * a *different* MEM button still reads the fresh cur_pos. */
        return;
    }

    if (diff > 0 && cur_pos <= (RADIATOR_MIN_POS + RADIATOR_BRAKE_MARGIN)) {
        // Prevent starting downward motion if already at or below safe limit
        return;
    }

    mem_target = target;
    mem_entry_pos = cur_pos;
    if (diff < 0) {
        bui_state = BUI_MEM_UP;
        bctrl_set_target(BCTRL_UP);
    } else {
        bui_state = BUI_MEM_DOWN;
        bctrl_set_target(BCTRL_DOWN);
    }
}

/**
 * Initialize from stored position values.
 *
 * Called from mainline (main.c calls InitApp which calls bui_init).
 * EEPROM reads from raw registers are NOT __eeprom-qualified, so
 * XC8 does not consider this an interrupt/mainline conflict.
 *
 * A fresh (never-written) chip has both EEPROM words at 0xFFFF, which
 * signed-casts to (int16_t)-1; we treat that as NULL/zero so the
 * first save populates both slots per the spec in README.md.
 */
void bui_init(void) {
    low_pos  = (int16_t)eeprom_load16(EEPROM_ADDR_LOW_POS);
    high_pos = (int16_t)eeprom_load16(EEPROM_ADDR_HIGH_POS);

    if (low_pos < 0 && high_pos < 0) {
        /* Fresh chip — never written. */
        low_pos  = 0;
        high_pos = 0;
    } else {
        // Enforce radiator lower limit on stored positions
        if (low_pos > 0 && low_pos < RADIATOR_MIN_POS) {
            low_pos = RADIATOR_MIN_POS;
        }
        if (high_pos > 0 && high_pos < RADIATOR_MIN_POS) {
            high_pos = RADIATOR_MIN_POS;
        }
    }
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
                // Prevent starting DOWN if already at or below radiator limit
                if (cur_pos > (RADIATOR_MIN_POS + RADIATOR_BRAKE_MARGIN)) {
                    bui_state = BUI_DOWN;
                    bctrl_set_target(BCTRL_DOWN);
                }
            } else if (input == INPUT_MEM_UP || input == INPUT_MEM_DOWN) {
                /* Always-accept memory recall: the press declares an
                 * intent (go to upper / lower), and recall_memory()
                 * chooses the direction so the desk converges on the
                 * target from any starting position. */
                int16_t target = (input == INPUT_MEM_UP) ? high_pos : low_pos;
                recall_memory(target);
            } else if (input == INPUT_SAVE) {
                /* Defer the EEPROM write to bui_service() (mainline). */
                pending_save_pos = cur_pos;
            } else if (input == INPUT_FACTORY_RESET) {
                pending_factory_reset = true;
            }
            break;

        case BUI_UP:
            if (input == INPUT_MEM_UP || input == INPUT_MEM_DOWN) {
                int16_t target = (input == INPUT_MEM_UP) ? high_pos : low_pos;
                recall_memory(target);
            } else if (input == INPUT_IDLE) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            } else if (input == INPUT_DOWN) {
                if (cur_pos > (RADIATOR_MIN_POS + RADIATOR_BRAKE_MARGIN)) {
                    bui_state = BUI_DOWN;
                    bctrl_set_target(BCTRL_DOWN);
                } else {
                    bui_state = BUI_STOP;
                    bctrl_set_target(BCTRL_STOP);
                }
            }
            break;

        case BUI_DOWN:
            if (input == INPUT_MEM_DOWN || input == INPUT_MEM_UP) {
                int16_t target = (input == INPUT_MEM_UP) ? high_pos : low_pos;
                recall_memory(target);
            } else if (input == INPUT_IDLE) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            } else if (input == INPUT_UP) {
                bui_state = BUI_UP;
                bctrl_set_target(BCTRL_UP);
            }
            break;

        case BUI_MEM_UP:
            if (input == INPUT_MEM_DOWN) {
                /* Switch mid-recall to the lower target. */
                recall_memory(low_pos);
            } else if (input == INPUT_UP) {
                bui_state = BUI_UP;
            } else if (input == INPUT_DOWN) {
                if (cur_pos > (RADIATOR_MIN_POS + RADIATOR_BRAKE_MARGIN)) {
                    bui_state = BUI_DOWN;
                    bctrl_set_target(BCTRL_DOWN); // switch directions
                } else {
                    bui_state = BUI_STOP;
                    bctrl_set_target(BCTRL_STOP);
                }
            }
            break;

        case BUI_MEM_DOWN:
            if (input == INPUT_MEM_UP) {
                /* Switch mid-recall to the upper target. */
                recall_memory(high_pos);
            } else if (input == INPUT_UP) {
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

    // Hard radiator collision limit: check if moving downwards (manual or memory)
    if ((bui_state == BUI_DOWN || bui_state == BUI_MEM_DOWN) &&
        cur_pos <= (RADIATOR_MIN_POS + RADIATOR_BRAKE_MARGIN)) {
        bui_state = BUI_STOP;
        bctrl_set_target(BCTRL_STOP);
        return;
    }

    switch (bui_state) {
        case BUI_MEM_UP: {
            /* Fire STOP this far short of `mem_target`.  The estimate
             * ramps up with how far the desk has travelled in this
             * recall (proxy for accumulated momentum), capped at the
             * ivanwick-calibrated decel-overrun value.  The desk's
             * post-STOP coast lands near the target — whether we
             * arrived from below or from above (the latter happens
             * when the user presses MEM_UP from a position higher
             * than `high_pos`, which reverses the direction). */
            int16_t overrun = overrun_for(cur_pos - mem_entry_pos);
            if (cur_pos >= mem_target - overrun) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            }
            break;
        }
        case BUI_MEM_DOWN: {
            int16_t overrun = overrun_for(mem_entry_pos - cur_pos);
            if (cur_pos <= mem_target + overrun) {
                bui_state = BUI_STOP;
                bctrl_set_target(BCTRL_STOP);
            }
            break;
        }
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
 *
 * Slot selection follows the spec in the README:
 *
 *   Stage 0 (fresh, both slots == 0)
 *     -> write save_pos to BOTH slots, so either memory button
 *        moves there.
 *
 *   Stage 1 (one memory, both slots equal)
 *     -> smaller-than-stored goes to LOW; larger-or-equal goes to
 *        HIGH. After this transition the two slots differ and the
 *        BUI is in Stage 2.
 *
 *   Stage 2 (two distinct memories)
 *     -> closer-to-the-smaller goes to LOW (incl. when save_pos is
 *        smaller than both); closer-to-the-bigger goes to HIGH
 *        (incl. when save_pos is bigger than both).
 *
 * The stored value is the actual save position (no decel-margin
 * offset).  The BUI_MEM_* handlers stop the desk when cur_pos
 * reaches the stored value; small mechanical decel overshoot
 * remains but is unnoticeable.
 *
 * If the given save_pos looks like an encoder error state (negative),
 * nothing is saved.
 *
 * @param save_pos new encoder value to store into persistent memory
 */
void bui_save_pos(int16_t save_pos) {
    if (save_pos < 0) {
        // Encoder value indicates error state
        return;
    }

    // Do not allow saving positions lower than radiator limit
    if (save_pos < RADIATOR_MIN_POS) {
        save_pos = RADIATOR_MIN_POS;
    }

    if (low_pos == 0 && high_pos == 0) {
        /* Stage 0 -> Stage 1: first save populates both slots with
         * the same value. */
        low_pos  = save_pos;
        high_pos = save_pos;
        eeprom_store16(EEPROM_ADDR_LOW_POS,  (uint16_t)low_pos);
        eeprom_store16(EEPROM_ADDR_HIGH_POS, (uint16_t)high_pos);
    } else if (low_pos == high_pos) {
        /* Stage 1 -> Stage 2: a/b rule. save_pos smaller than
         * stored becomes the new low; save_pos >= stored becomes
         * the new high. After this low < high. */
        if (save_pos < low_pos) {
            low_pos = save_pos;
            eeprom_store16(EEPROM_ADDR_LOW_POS, (uint16_t)low_pos);
        } else {
            high_pos = save_pos;
            eeprom_store16(EEPROM_ADDR_HIGH_POS, (uint16_t)high_pos);
        }
    } else if (abs(low_pos - save_pos) < abs(high_pos - save_pos)) {
        /* Stage 2: closer to the smaller stored value (includes
         * when save_pos is smaller than both). */
        low_pos = save_pos;
        eeprom_store16(EEPROM_ADDR_LOW_POS, (uint16_t)low_pos);
    } else {
        /* Closer to the larger stored value (or equal). */
        high_pos = save_pos;
        eeprom_store16(EEPROM_ADDR_HIGH_POS, (uint16_t)high_pos);
    }

    // Click feedback
    bctrl_set_target(BCTRL_CLICK);
}

/**
 * Factory-reset handler.
 *
 * Requested by btn.c after the user holds both buttons for 8 seconds
 * (the INPUT_FACTORY_RESET event, latched by bui_input) and executed
 * from mainline by bui_service().  Clears all four EEPROM position
 * slots to 0xFFFF and forces the in-memory low_pos / high_pos to
 * (0, 0), so the next INPUT_SAVE populates both slots per the
 * fresh-chip save rule in `bui_save_pos``.
 *
 * Sequence:
 *   1. eeprom_factory_reset() — writes 0xFFFF to EEPROM[0x00..0x07].
 *   2. Update in-memory low_pos / high_pos to (0, 0).
 *   3. Single relay click as audible feedback (same as bui_save_pos).
 *
 * See oem_reconstruction/docs/FACTORY_RESET.md for the OEM firmware's
 * own factory-reset behaviour (it wrote OEM endstop values instead).
 */
void bui_factory_reset(void) {
    eeprom_factory_reset();

    low_pos  = 0;
    high_pos = 0;

    bctrl_set_target(BCTRL_CLICK);
}
