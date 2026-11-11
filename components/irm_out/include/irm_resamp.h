/* Streaming-Resampler Mono -> Stereo fuer Quellen mit fester, aber nicht exakt bekannter Rate (KiwiSDR: nominell 12 kHz, real z. B. 11998,895 Hz).
 * Reine C-Komponente ohne IDF-Abhaengigkeit -> host-testbar (test/test_resamp.c).
 *
 *  - Interpolation: Polyphasen-Sinc (24 Taps, Kaiser-Fenster beta 6,8, 64 Phasen, zwischen den Phasen linear interpoliert), Ganzzahl-
 *    Arithmetik im Datenpfad. Durchlassbereich bis ca. 0,41 x Quellrate (bei 12 kHz: 4,9 kHz = Kiwi-AM-Bandbreite), Spiegelspektren
 *    oberhalb 0,59 x Quellrate werden um >60 dB unterdrueckt (Messwerte: test/test_resamp.c). Die erste Ausgabe erscheint nach 12 Eingangs-
 *    samples (1 ms bei 12 kHz). Fuer Aufwaertsabtastung (Quellrate < Zielrate) ausgelegt; fuer Abwaertsabtastung fehlt der Anti-Alias-Filter.
 *  - Quellrate in Milli-Hz: das Verhaeltnis ist damit exakt (kein Aufrunden auf 12000 Hz).
 *  - Drift-Trim: Quelle (Kiwi-Takt) und Senke (I2S-Takt) laufen nie exakt gleich schnell. Der Trim aendert das Verhaeltnis um wenige ppm,
 *    geregelt aus dem Fuellstand des Jitterpuffers (irm_resamp_trim_ppm). Das haelt den Puffer ohne hoerbare Tonhoehenaenderung
 *    (1000 ppm = 1,7 Cent) im Soll, statt dass er nach Minuten leerlaeuft oder ueberlaeuft. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRM_RESAMP_TAPS             24
#define IRM_RESAMP_TRIM_MAX_PPM     1000
#define IRM_RESAMP_TRIM_PPM_PER_MS  8      /* Reglerverstaerkung: ppm je ms Abweichung des gemittelten Fuellstands */

typedef struct {
    int16_t  w[IRM_RESAMP_TAPS];   /* juengste Eingangssamples; die Ausgabeposition liegt zwischen w[TAPS/2-1] und w[TAPS/2] */
    uint64_t frac;       /* Position dazwischen, Q32; >= 1.0 heisst: erst neue Eingangswerte holen */
    uint64_t step_nom;   /* Quellrate / Zielrate, Q32 */
    uint64_t step;       /* step_nom mit Trim */
} irm_resamp_t;

void irm_resamp_init(irm_resamp_t *r, uint32_t src_rate_mhz, uint32_t dst_rate_hz);
/** Trim in ppm (positiv = Quelle schneller verbrauchen). Wird auf +-IRM_RESAMP_TRIM_MAX_PPM begrenzt. */
void irm_resamp_set_trim_ppm(irm_resamp_t *r, int32_t ppm);

/**
 * Verarbeitet Eingangssamples und schreibt Stereo (L = R) mit Verstaerkung (Q15, 32768 = 1,0) und harter Begrenzung.
 * Endet, wenn die Eingabe verbraucht oder @p cap_frames erreicht sind; *used = verbrauchte Eingangssamples.
 * @return Anzahl geschriebener Frames. Aufteilen der Eingabe in beliebige Bloecke aendert das Ergebnis nicht.
 */
size_t irm_resamp_mono_to_stereo(irm_resamp_t *r, const int16_t *in, size_t n_in, size_t *used,
                                 int16_t *out_stereo, size_t cap_frames, uint32_t gain_q15);

/** Obergrenze fuer Ausgabeframes bei n_in Eingangssamples (inkl. Trim-Reserve und Rundung), zur Dimensionierung des Ausgabepuffers. */
size_t irm_resamp_max_frames(uint32_t src_rate_mhz, uint32_t dst_rate_hz, size_t n_in);

/** P-Regler: Abweichung des gemittelten Fuellstands vom Soll (ms, positiv = zu voll) -> Trim in ppm. */
int32_t irm_resamp_trim_ppm(int32_t fill_err_ms);

/** Gleitender Mittelwert des Fuellstands (Zeitkonstante 256 Aufrufe, Zustand in Q8). Bei (Wieder-)Start *avg_q8 = Sollwert_ms << 8 setzen.
 *  Ohne Glaettung wuerde das ruckartige Eintreffen der Kiwi-Frames (alle 43 ms bzw. 171 ms) den Regler modulieren. @return Mittelwert in ms. */
int32_t irm_resamp_avg_update(int32_t *avg_q8, int32_t fill_ms);

#ifdef __cplusplus
}
#endif
