/* Netzwerk-Audioeingang als iRadioMini2-Quelle: nimmt rohes PCM (s16le, stereo, IRM_NETIN_RATE) per UDP oder TCP auf IRM_NETIN_PORT entgegen und gibt es
 * ueber dieselbe irm_out_sink_t aus wie irm_audio/irm_kiwi (die Engines duerfen die Senke NIE gleichzeitig nutzen: irm_control stoppt die jeweils anderen).
 *
 *  - Die Ports sind nur zwischen irm_netin_play() und irm_netin_stop() offen.
 *  - Nur Absender aus dem Subnetz der WLAN-Schnittstelle; immer nur einer gleichzeitig (siehe irm_netin_policy.h). Kein Passwort.
 *  - Jitterpuffer + Stereo-Resampler mit Driftregelung (Fuellstand -> +-1000 ppm), damit PC- und I2S-Takt auseinanderlaufen duerfen.
 *  - TCP: ein voller Puffer bremst den Sender (es wird nicht mehr gelesen); UDP: dann werden Pakete verworfen (Zaehler overrun_bytes).
 * Alle Funktionen sind thread-sicher; gedacht fuer den Aufruf aus dem Control-Task. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "irm_out.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IRM_NETIN_EVT_RUNNING,   /* Ports offen, Senke offen: bereit (ein Absender ist noch nicht noetig) */
    IRM_NETIN_EVT_ERROR,     /* Sitzung beendet (Ports nicht bindbar, Senke nicht oeffenbar, ...) */
} irm_netin_evt_type_t;

typedef struct { irm_netin_evt_type_t type; int err; } irm_netin_evt_t;

/** Wird aus dem Netz-Task gerufen: kurz halten, nicht blockieren (irm_control postet nur ein Kommando). */
typedef void (*irm_netin_cb_t)(const irm_netin_evt_t *evt, void *ctx);

typedef struct {
    uint32_t rx_bytes;        /* angenommene Nutzdaten */
    uint32_t overrun_bytes;   /* wegen vollem Puffer verworfen (UDP) */
    uint32_t underruns;       /* Puffer lief waehrend aktivem Absender leer */
    uint32_t denied_packets;  /* abgelehnt: fremdes Subnetz oder anderer Absender aktiv */
    int32_t  fill_ms;         /* gemittelter Fuellstand */
    int32_t  trim_ppm;        /* aktuelle Driftkorrektur */
} irm_netin_stats_t;

esp_err_t irm_netin_init(irm_out_sink_t *sink, irm_netin_cb_t cb, void *ctx);
/** Ports und Senke oeffnen. Kehrt sofort zurueck; das Ergebnis kommt als Ereignis. */
esp_err_t irm_netin_play(void);
/** Beendet die Sitzung, schliesst Ports und gibt die Senke frei (blockiert hoechstens ca. 2,5 s). Ohne Sitzung sofort zurueck. Sendet kein Ereignis. */
esp_err_t irm_netin_stop(void);
void irm_netin_get_stats(irm_netin_stats_t *out);

uint16_t irm_netin_port(void);
uint32_t irm_netin_rate(void);   /* erwartete Abtastrate des Eingangsstroms */

#ifdef __cplusplus
}
#endif
