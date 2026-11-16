#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "irm_playlist.h"

#ifdef __cplusplus
extern "C" {
#endif
/** Datei vollstaendig lesen (max_size als Schutz). Aufrufer gibt *buf mit free() frei. 0-terminiert. */
esp_err_t irm_files_read(const char *path, size_t max_size, char **buf, size_t *len);
esp_err_t irm_files_write(const char *path, const void *data, size_t len);
/** Playlist (m3u) von Datei laden. */
/** Haengt "#EXTINF:-1,<name>\n<uri>\n" an die M3U-Datei an (legt sie mit "#EXTM3U" an, falls sie fehlt). Zeilenumbrueche im Namen werden entfernt. */
esp_err_t irm_files_append_playlist(const char *path, const char *name, const char *uri);
/** Entfernt das (nth+1)-te Vorkommen von `uri` samt zugehoeriger #EXTINF-Zeile aus der M3U-Datei; alles andere bleibt unveraendert.
 *  Geschrieben wird in "<path>.tmp" und dann umbenannt, ein Abbruch laesst die alte Datei intakt.
 *  ESP_ERR_NOT_FOUND: Datei oder Eintrag nicht vorhanden (Datei unveraendert); ESP_ERR_INVALID_SIZE: Datei groesser als 64 KiB; ESP_FAIL: Schreibfehler. */
esp_err_t irm_files_remove_playlist_entry(const char *path, const char *uri, size_t nth);
esp_err_t irm_files_load_playlist(const char *path, irm_playlist_t *pl);
/** Verzeichnisbaum nach Audiodateien durchsuchen (Endungen kommagetrennt, z.B. "mp3,aac,m4a,wav,flac"). */
esp_err_t irm_files_scan_media(const char *root, const char *extensions, int max_depth, irm_playlist_t *pl);
#ifdef __cplusplus
}
#endif
