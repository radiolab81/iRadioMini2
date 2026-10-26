/* Reine C-Komponente ohne IDF-Abhaengigkeit -> auf dem Host testbar. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRM_PLAYLIST_URI_MAX   255
#define IRM_PLAYLIST_NAME_MAX  63

typedef struct irm_playlist irm_playlist_t;

irm_playlist_t *irm_playlist_new(size_t max_items);
void            irm_playlist_free(irm_playlist_t *pl);
size_t          irm_playlist_count(const irm_playlist_t *pl);

/** URI des Eintrags oder NULL bei ungueltigem Index. */
const char *irm_playlist_uri(const irm_playlist_t *pl, size_t idx);
/** Anzeigename (aus #EXTINF) oder, falls keiner vorhanden, die URI. */
const char *irm_playlist_name(const irm_playlist_t *pl, size_t idx);

/** Eintrag anhaengen. Leere/zu lange URIs werden abgelehnt. name darf NULL sein. */
bool irm_playlist_add(irm_playlist_t *pl, const char *uri, const char *name);

/**
 * M3U/Textliste parsen. Unterstuetzt LF/CRLF, UTF-8-BOM, Leerzeilen, '#'-Kommentare
 * und "#EXTINF:<dauer>,<name>" als Anzeigename fuer die folgende URI.
 * Verschachtelte Playlisten werden nicht aufgeloest.
 * @return Anzahl hinzugefuegter Eintraege.
 */
size_t irm_playlist_parse_m3u(irm_playlist_t *pl, const char *text, size_t len);

#ifdef __cplusplus
}
#endif
