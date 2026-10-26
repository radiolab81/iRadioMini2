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
esp_err_t irm_files_load_playlist(const char *path, irm_playlist_t *pl);
/** Verzeichnisbaum nach Audiodateien durchsuchen (Endungen kommagetrennt, z.B. "mp3,aac,m4a,wav,flac"). */
esp_err_t irm_files_scan_media(const char *root, const char *extensions, int max_depth, irm_playlist_t *pl);
#ifdef __cplusplus
}
#endif
