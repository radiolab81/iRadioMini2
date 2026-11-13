/* Serververzeichnis (KiwiSDR-Liste) fuer iRadioMini2: Flash-Datenbank, Update im Hintergrund, Zugriff fuer Web/Control.
 * Aufgabenteilung: kiwi_dir (portabel, unveraendert aus ESP32_KiwiSDR) = Datenbank/Parser/HTTP; irm_dir_query.h = Suche/Blaettern (reines C);
 * diese Schicht = Partition, Update-Task, Zustand. Siehe docs/KIWI.md.
 * Thread-Sicherheit: irm_dir_db()/irm_dir_get()/irm_dir_info() duerfen aus jedem Task laufen, auch waehrend eines Updates. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "irm_dir_query.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Einmal beim Start: Partition "kiwidb" suchen, Datenbank oeffnen (mit Pruefsumme). Eine fehlende Partition ist kein Fehler (available=false). */
esp_err_t irm_dir_init(void);

/** Datenbank fuer irm_dir_search()/irm_dir_countries(). Nie NULL (nach init); bei fehlender Partition leer. */
const kiwi_dir_db_t *irm_dir_db(void);

void irm_dir_info(irm_dir_info_t *out);

/** Liest Eintrag @p idx der aktuellen Datenbank. */
bool irm_dir_get(uint32_t idx, kiwi_dir_rec_t *rec);

/**
 * Startet das Aktualisieren der Liste im Hintergrund (ca. 0,9 MB per HTTP, wenige Sekunden; Fortschritt/Ergebnis in irm_dir_info()).
 * Waehrend der ca. 2-3 s Flash-Loeschphase sind kurze Tonaussetzer moeglich (siehe dir_store_flash im Original).
 * @return ESP_OK gestartet; ESP_ERR_NOT_SUPPORTED keine Partition; ESP_ERR_INVALID_STATE laeuft bereits;
 *         ESP_ERR_TIMEOUT Mindestabstand noch nicht erreicht (ohne force; irm_dir_info().retry_in_s); ESP_ERR_NO_MEM Task nicht startbar
 */
esp_err_t irm_dir_update(bool force);

#ifdef __cplusplus
}
#endif
