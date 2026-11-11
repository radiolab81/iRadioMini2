/* KiwiSDR-Eintraege (URI -> Server, Frequenz, Modus) und Modus-Tabelle. Reine C-Komponente ohne IDF-Abhaengigkeit -> auf dem Host testbar.
 *
 * Unterstuetzte Schreibweisen (Schluessel "f" wie in der Adresszeile des Kiwi-Browsers; Frequenz in kHz):
 *   kiwi://host[:port]/?f=7055.5usb[&pw=geheim]
 *   kiwi://host[:port]/?f=7055.5&mode=usb
 *   http://host:8073/?f=225am        (aus dem Browser kopiert; "z<n>" hinter dem Modus = Zoom, wird ignoriert)
 * Standardport: kiwi:// -> 8073, http:// und ws:// -> 80 (wie im Browser). Ohne Modus gilt "am". */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRM_KIWI_HOST_MAX     64
#define IRM_KIWI_PW_MAX       32
#define IRM_KIWI_MODE_MAX     8      /* inkl. NUL, passt zu kiwi_client_t.cur_mode */
#define IRM_KIWI_FREQ_MIN_HZ  10000u
#define IRM_KIWI_FREQ_MAX_HZ  32000000u

typedef struct {
    char     host[IRM_KIWI_HOST_MAX];
    uint16_t port;
    uint32_t freq_hz;
    char     mode[IRM_KIWI_MODE_MAX];     /* Kleinbuchstaben, aus der Modus-Tabelle */
    char     password[IRM_KIWI_PW_MAX];   /* "" = oeffentlicher Kiwi */
} irm_kiwi_uri_t;

/** URI zerlegen. false bei unbekanntem Schema, ungueltigem Host/Port, fehlender/ausserhalb 10 kHz..32 MHz liegender Frequenz
 *  oder unbekanntem Modus. IPv6-Literale werden nicht unterstuetzt. */
bool irm_kiwi_uri_parse(const char *uri, irm_kiwi_uri_t *out);

/** Mono-Demodulationsmodi (gleiche Reihenfolge/Namen wie kiwi_core; der Host-Test gleicht beides ab). */
size_t      irm_kiwi_mode_count(void);
const char *irm_kiwi_mode_name(size_t idx);          /* NULL bei ungueltigem Index */
int         irm_kiwi_mode_index(const char *name);   /* Gross/Kleinschreibung egal, -1 = unbekannt */

/** Frequenz als kHz-Text ohne Nachkomma-Nullen: 7055000 -> "7055", 7055500 -> "7055.5", 77500 -> "77.5". */
void irm_kiwi_fmt_khz(char *out, size_t n, uint32_t hz);

#ifdef __cplusplus
}
#endif
