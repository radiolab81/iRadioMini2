#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "irm_dirlist.h"
#include "irm_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/** Startet die Steuer-Task. Voraussetzung: irm_core, irm_settings, irm_board (Codec) initialisiert. */
esp_err_t irm_control_start(void);
/** Thread-sichere Lesezugriffe auf die Listen (z.B. fuer Web-UI). */
size_t irm_control_item_count(irm_source_t src);
bool   irm_control_item(irm_source_t src, size_t idx, char *name, size_t name_sz, char *uri, size_t uri_sz);
/** Dateibrowser (Quelle Dateien): eine Seite des SD-Ordners r->path als JSON (siehe irm_dirlist_json). *json mit free() freigeben.
 *  ESP_ERR_NOT_FOUND: keine SD-Karte oder Ordner nicht vorhanden; ESP_ERR_NO_MEM. Thread-sicher, beruehrt den Zustandsautomaten nicht. */
esp_err_t irm_control_browse(const irm_dl_req_t *r, char **json);
/** Datei (Pfad relativ zur SD-Wurzel, z. B. "/Rock/a.mp3") abspielen: Quelle wird Dateien, die Playlist besteht aus den Dateien dieses Ordners
 *  (alphabetisch), Weiter/Zurueck/Titelende folgen darin. ESP_ERR_INVALID_ARG: unzulaessiger Pfad; ESP_ERR_NOT_FOUND: keine SD-Karte/keine abspielbare Datei. */
esp_err_t irm_control_play_path(const char *rel_path);
#ifdef __cplusplus
}
#endif
