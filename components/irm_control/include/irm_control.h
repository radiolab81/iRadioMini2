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

/** Datei (Pfad relativ zur SD-Wurzel) loeschen. Laeuft sie gerade, wird sie angehalten und der naechste Titel gespielt; sie verschwindet aus der Wiedergabeliste.
 *  Nur Dateien mit Mediaendung (IRM_MEDIA_EXTENSIONS). Laeuft die Aufnahme in diese Datei, wird abgelehnt. Blockiert bis zu ca. 6 s auf den Steuer-Task.
 *  ESP_OK; ESP_ERR_INVALID_ARG: unzulaessiger Pfad; ESP_ERR_NOT_FOUND: keine SD-Karte/Datei fehlt; ESP_ERR_NOT_SUPPORTED: Datei wird gerade aufgenommen;
 *  ESP_FAIL: Dateisystemfehler; ESP_ERR_TIMEOUT. */
esp_err_t irm_control_file_delete(const char *rel_path);

/** Datei umbenennen (im selben Ordner). new_name = nur der Name (irm_filename_valid: keine Pfadtrenner/FAT-Sonderzeichen, Mediaendung bleibt Pflicht).
 *  Laeuft die Datei gerade, startet sie danach von vorn. Wie irm_control_file_delete, zusaetzlich ESP_ERR_INVALID_STATE: Zielname existiert schon. */
esp_err_t irm_control_file_rename(const char *rel_path, const char *new_name);

/** Kiwi-Favorit speichern (wie IRM_CMD_KIWI_FAVORITE), aber unter dem Namen `name` (zugeschnitten auf 63 Bytes, Steuerzeichen werden zu Leerzeichen);
 *  NULL oder leer = Name des laufenden Eintrags. */
esp_err_t irm_control_kiwi_favorite(const char *name);

/** Anzahl der gespeicherten Kiwi-Favoriten (Eintraege der Liste vor einem eventuellen Direkteintrag; nur diese lassen sich mit IRM_CMD_KIWI_DELETE loeschen). */
size_t irm_control_kiwi_favorites(void);
#ifdef __cplusplus
}
#endif
