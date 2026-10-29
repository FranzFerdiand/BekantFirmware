#ifndef RADIATOR_CONFIG_H
#define RADIATOR_CONFIG_H

#include <stdint.h>

/**
 * --- HEIZKÖRPER-KOLLISIONSSCHUTZ FÜR IKEA BEKANT ---
 *
 * Berechnung der Encoder-Ticks:
 *   Unterster Werksanschlag (~64 cm): ~150 Ticks (0x0096)
 *   Standard-Sitzhöhe       (~70 cm): 1590 Ticks (0x0636)
 *   Standard-Stehhöhe      (~110 cm): 5632 Ticks (0x1600)
 *   Faktor:                 1 cm    ~ 100 Ticks
 *
 * Formel:
 *   Ziel-Ticks = 1590 + (Wunsch-Minimalhöhe_in_cm - 70) * 100
 *
 * Beispiel:
 *   73 cm Minimalhöhe (inkl. 2 cm Sicherheitsabstand zum Heizkörper):
 *   1590 + (73 - 70) * 100 = 1890 Ticks = 0x0762
 */

// Konfigurierbare Mindesthöhe (Standard: 0x0762 = ca. 73 cm)
#define RADIATOR_MIN_POS        ((int16_t)0x0762)

// Bremsweg-Reserve (0x0050 = ca. 80 Ticks = ca. 8 mm Bremsauslauf für sanftes Stoppen)
#define RADIATOR_BRAKE_MARGIN   ((int16_t)0x0050)

#endif /* RADIATOR_CONFIG_H */
