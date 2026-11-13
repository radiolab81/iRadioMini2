/* Web-Schnittstelle des Serververzeichnisses: Query-Parser, UTF-8-Bereinigung, JSON-Erzeugung. Ohne HTTP-Server-Abhaengigkeit (nur cJSON)
 * und damit host-testbar (test/test_irm_web_dir.c). Die HTTP-Handler selbst stehen in irm_web.c. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#include "irm_dir_query.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IRM_WEB_DIR_PAGE_DEFAULT 20
#define IRM_WEB_DIR_PAGE_MAX     40   /* je Treffer ca. 0,2 KB JSON und 120 B Puffer: bleibt klein genug fuer den 6-KB-Stack des HTTP-Servers (Puffer liegt auf dem Heap) */

typedef struct { irm_dir_filter_t f; uint32_t offset, limit; } irm_web_dir_req_t;

/**
 * Query-String ohne fuehrendes '?' parsen:  text=<..>&country=<0-255>&snr=<0-99>&free=<0|1>&freq=<Hz>&sort=<list|snr>&offset=<n>&limit=<1..40>
 * Prozent- und Plus-Kodierung wird aufgeloest; unbekannte Schluessel werden ignoriert; qs == NULL ergibt die Vorgaben.
 * @return false bei ungueltigem Wert (dann 400 Bad Request).
 */
bool irm_web_dir_parse_query(const char *qs, irm_web_dir_req_t *out);

/** Macht s zu gueltigem UTF-8 (in place, wird nie laenger): ungueltige Bytes -> '?', Steuerzeichen -> ' '. Fremde Daten (Verzeichnisdienst)
 *  duerfen weder das JSON noch die Seite brechen. @return Laenge. */
size_t irm_web_utf8_clean(char *s);

/** {"total":N,"offset":o,"limit":l,"items":[{"i","name","place","country","host","port","users","max","snr","lo","hi"}...]}  NULL bei Speichermangel. */
cJSON *irm_web_dir_list_json(const kiwi_dir_db_t *db, const irm_web_dir_req_t *r);
/** [{"id":n,"name":"Germany","n":23}, ...] alphabetisch, nur Laender mit verbindbaren Servern. */
cJSON *irm_web_dir_countries_json(const kiwi_dir_db_t *db);
/** {"available","valid","count","generation","state":"idle|updating|ok|failed","bytes","servers","err","msg","retry_in_s"} */
cJSON *irm_web_dir_info_json(const irm_dir_info_t *i);

#ifdef __cplusplus
}
#endif
