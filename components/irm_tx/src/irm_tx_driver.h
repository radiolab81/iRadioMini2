#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Gemeinsame Schnittstelle der Traegertreiber (Altprojekt: vier Treiber mit identischen Symbolen initTX/setTxFreq,
 * die per Linker-Auswahl getauscht wurden). Jetzt: genau ein Treiber per Kconfig, gleiche Schnittstelle. */
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*set_freq_hz)(uint32_t hz);
} irm_tx_driver_t;

extern const irm_tx_driver_t irm_tx_driver;
