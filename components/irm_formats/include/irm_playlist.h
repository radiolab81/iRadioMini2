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

/** URI und Name des Eintrags idx ersetzen (z. B. nach Umbenennen einer Datei). name darf NULL sein. false bei ungueltigem Index oder leerer/zu langer URI (Eintrag bleibt dann unveraendert). */
bool irm_playlist_set(irm_playlist_t *pl, size_t idx, const char *uri, const char *name);

/** Eintrag idx entfernen; die folgenden Eintraege ruecken nach. false bei ungueltigem Index. */
bool irm_playlist_remove(irm_playlist_t *pl, size_t idx);

/**
 * Aus M3U-Text (in place) den Eintrag mit der URI `uri` entfernen: die URI-Zeile samt der #EXTINF-Zeile, die der Parser ihr zuordnet.
 * Entfernt wird das (nth+1)-te Vorkommen der URI (nth = Anzahl frueherer Vorkommen, die stehen bleiben).
 * Alle uebrigen Zeilen (Kommentare, Leerzeilen, ungueltige Eintraege, Zeilenenden, BOM) bleiben unveraendert.
 * @param len  in: Laenge des Textes, out: neue Laenge (kein Terminator wird geschrieben)
 * @return true, wenn ein Eintrag entfernt wurde
 */
bool irm_m3u_remove_entry(char *text, size_t *len, const char *uri, size_t nth);

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
