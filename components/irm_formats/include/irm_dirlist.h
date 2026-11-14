/* Dateibrowser-Bausteine ohne IDF-Abhaengigkeit (nur POSIX dirent/stat) -> auf dem Host testbar (test/test_dirlist.c).
 *  - Pfadpruefung fuer Pfade aus dem Netz (kein "..", keine Steuerzeichen, begrenzte Laenge)
 *  - seitenweises, sortiertes Verzeichnislisting mit Cursor (Ordner zuerst, dann Dateien, jeweils alphabetisch) bei O(Seitengroesse) Speicher
 *  - JSON-Ausgabe (UTF-8-bereinigt, escaped) und Abfrage-Parser fuer die Web-API */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "irm_playlist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IRM_DL_NAME_MAX   100   /* Bytes je Datei-/Ordnername inkl. 0; laengere Namen werden nicht angezeigt (sie waeren als Pfad ohnehin kaum adressierbar) */
#define IRM_DL_PATH_MAX   200   /* relativer Pfad (ab SD-Wurzel), ohne 0 */
#define IRM_DL_PAGE_MAX   40
#define IRM_DL_PAGE_DEFAULT 30
#define IRM_DL_DIRS   1
#define IRM_DL_FILES  2

typedef struct { bool dir; char name[IRM_DL_NAME_MAX]; } irm_dl_entry_t;

typedef struct {
    char     path[IRM_DL_PATH_MAX + 1];   /* normalisiert, beginnt mit '/', "/" = Wurzel */
    bool     has_after, after_dir;        /* Cursor: Eintrag, nach dem die Seite beginnt */
    char     after[IRM_DL_NAME_MAX];
    uint32_t limit;
} irm_dl_req_t;

/** "/a//b/./c/" -> "/a/b/c". Verlangt fuehrendes '/'. false bei "..", '\\', Steuerzeichen, Segment >= IRM_DL_NAME_MAX oder zu langem Ergebnis. */
bool irm_path_normalize(const char *in, char *out, size_t cap);
/** uri = root + normalisierter Pfad -> Pfad relativ zur Wurzel ("/" wenn uri == root). false, wenn uri nicht unter root liegt. */
bool irm_path_strip_root(const char *uri, const char *root, char *out, size_t cap);

/** Endungsliste kommagetrennt ("mp3,aac"), Gross-/Kleinschreibung egal. */
bool irm_dirlist_ext_match(const char *name, const char *exts);

/**
 * Query ohne '?':  path=<..>&after=<d:|f:>Name&limit=<1..40>   (Prozent-/Plus-Kodierung wird aufgeloest, unbekannte Schluessel ignoriert).
 * Ohne path gilt "/". @return false bei ungueltigem Pfad/Cursor/Limit (-> 400).
 */
bool irm_dirlist_parse_query(const char *qs, irm_dl_req_t *out);

/**
 * Eine Seite des Verzeichnisses abs_dir: bis zu `limit` Eintraege, die hinter dem Cursor liegen (oder ab Anfang). Versteckte Eintraege (".name") entfallen,
 * Dateien nur mit passender Endung. flags: IRM_DL_DIRS | IRM_DL_FILES.
 * *total = alle passenden Eintraege des Ordners, *more = hinter der Seite folgt noch etwas (je darf NULL sein).
 * @return Anzahl Eintraege, -1 wenn der Ordner nicht geoeffnet werden kann.
 */
int irm_dirlist_page(const char *abs_dir, const char *exts, unsigned flags, bool has_after, bool after_dir, const char *after,
                     irm_dl_entry_t *out, size_t limit, size_t *total, bool *more);

/** Alle passenden Dateien (nicht rekursiv) in Ordnerreihenfolge (alphabetisch) als abs_dir/name an pl haengen; stoppt, wenn pl voll ist. @return false bei Fehler/Speichermangel. */
bool irm_dirlist_scan_sorted(const char *abs_dir, const char *exts, irm_playlist_t *pl);

/** {"path":"/a/b","total":n,"more":bool,"items":[{"n":"name","d":1},{"n":"x.mp3"}]}  malloc'd, NULL bei Speichermangel. */
char *irm_dirlist_json(const char *rel_path, const irm_dl_entry_t *e, size_t n, size_t total, bool more);

/** In place: gueltiges UTF-8 erzwingen (ungueltige Bytes -> '?', Steuerzeichen -> ' '). @return Laenge. */
size_t irm_dirlist_utf8_clean(char *s);

#ifdef __cplusplus
}
#endif
