/* MP4/M4A-Hilfen (reines C, host-testbar): erkennt Dateien, bei denen das "moov"-Atom hinter "mdat" liegt, und liefert sie
 * als virtuellen Datenstrom mit vorgezogenem moov ("faststart", wie qt-faststart) - ohne die Datei zu kopieren.
 * Hintergrund: Der M4A-Parser der Espressif-Decoderbibliothek unterstuetzt nur moov VOR mdat. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRM_MP4_MAX_MOOV  (8u * 1024u * 1024u)   /* Obergrenze fuer das im RAM gehaltene moov */

typedef struct {
    uint64_t file_size;
    uint64_t ftyp_end;       /* Ende des ftyp-Atoms am Dateianfang (0 wenn keines) */
    uint64_t mdat_start;     /* Beginn des ersten mdat */
    uint64_t moov_start, moov_end;
    bool     has_mdat, has_moov;
} irm_mp4_info_t;

/** Atom-Struktur der obersten Ebene lesen. 0 = ok, <0 = kein gueltiges MP4. */
int irm_mp4_scan(FILE *f, irm_mp4_info_t *info);
/** true, wenn moov hinter mdat liegt (Datei fuer den ESP-Parser nicht direkt abspielbar). */
bool irm_mp4_needs_faststart(const irm_mp4_info_t *info);

/** Dateiendung .m4a/.mp4/.m4b/.aac-in-mp4 (ohne Beruecksichtigung der Gross-/Kleinschreibung)? */
bool irm_mp4_has_mp4_extension(const char *path);

typedef struct irm_mp4_fs irm_mp4_fs_t;
/** Prueft eine Datei. *needs = true wenn irm_mp4_fs_open() noetig ist. 0 = ok, <0 = Fehler/kein MP4. */
int  irm_mp4_probe(const char *path, bool *needs);
/** Virtuelle Datei oeffnen (moov vorgezogen, Chunk-Offsets in stco/co64 angepasst). 0 = ok. */
int  irm_mp4_fs_open(const char *path, irm_mp4_fs_t **out);
uint64_t irm_mp4_fs_size(const irm_mp4_fs_t *fs);
int  irm_mp4_fs_seek(irm_mp4_fs_t *fs, uint64_t pos);
/** Liest bis zu n Bytes ab der aktuellen Position; 0 am Ende. */
size_t irm_mp4_fs_read(irm_mp4_fs_t *fs, void *buf, size_t n);
void irm_mp4_fs_close(irm_mp4_fs_t *fs);

#ifdef __cplusplus
}
#endif
