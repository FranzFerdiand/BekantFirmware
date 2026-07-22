
#include <stdint.h>

typedef enum {
    BCTRL_AFTER_SCAN,
    BCTRL_STOP,
    BCTRL_PRE_MOVE,
    BCTRL_UP,
    BCTRL_DOWN,
    BCTRL_UP_DECEL,
    BCTRL_DOWN_DECEL,
    BCTRL_PRE_STOP,
    BCTRL_CLICK,
} BCTRL_state_t;

/* BCMD values from the ivanwick baseline (git c9b048d) — proven on
 * hardware: UP and DOWN move the desk while the button is held.
 * The OEM bytes recovered from the 0x0d5 dispatcher (0xca, 0x0d, …)
 * turned out to belong to the encoder/endstop-check state machine,
 * not the direct motion commands — with 0xca the desk ignored UP
 * until button release. */
enum {
    BCMD_AFTER_SCAN_INIT = 0xbf,
    BCMD_STOP_IDLE       = 0xfc,  /* universal STOP (drops relay); matches OEM */
    BCMD_PRE_MOVE        = 0xc4,
    BCMD_UP              = 0x86,
    BCMD_DOWN            = 0x85,
    BCMD_DECEL           = 0x87,
    BCMD_PRE_STOP        = 0x84,
    BCMD_ERR_DOWN        = 0xbd,
    BCMD_ERR_UP          = 0xbc,
};

void bctrl_timer(void);
void bctrl_set_target(BCTRL_state_t state);
void bctrl_rx_lin(void);
void bctrl_init(void);

/* Current position (lifted from RAM, populated by bctrl_timer). */
extern int16_t bctrl_pos;
/* Current and target state (used by endstop detector). */
extern BCTRL_state_t current_state;
extern BCTRL_state_t target_state;

extern void (*bctrl_report_pos)(int16_t pos);

// BUI also needs this value so it can anticipate stops
#define BCTRL_DECEL_MARGIN 0x6c
