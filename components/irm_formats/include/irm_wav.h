/* Reine C-Hilfen fuer die Aufnahmefunktion (ohne IDF-Abhaengigkeit, host-testbar): WAV-Kopf und Dateinamen. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IRM_WAV_HDR_SIZE 44u
/** Groesste Datenmenge, die ein klassischer RIFF/WAV-Kopf (32 Bit) fassen kann (RIFF-Groesse = data + 36). */
#define IRM_WAV_DATA_MAX (0xFFFFFFFFu - 36u)

/** Schreibt den 44-Byte-Kopf fuer unkomprimiertes PCM (Format 1). data_bytes wird auf IRM_WAV_DATA_MAX begrenzt.
 *  Fuer eine laufende Aufnahme wird er mit dem aktuellen Stand regelmaessig neu geschrieben (Datei bleibt auch nach Stromausfall abspielbar). */
void irm_wav_header(uint8_t out[IRM_WAV_HDR_SIZE], uint32_t sample_rate, uint16_t channels, uint16_t bits, uint32_t data_bytes);

/** Dateiname einer Aufnahme: "<dir>/JJJJ-MM-TT_HH-MM-SS.wav" bzw. mit seq >= 2 "..._<seq>.wav" (Namenskollision in derselben Sekunde,
 *  z. B. beim Dateiwechsel). tm == NULL (Uhr nicht gestellt): "<dir>/rec_<seq, 4-stellig>.wav", seq 0 zaehlt wie 1.
 *  @return Laenge des Namens, 0 wenn er nicht in cap passt (out ist dann leer). */
size_t irm_rec_filename(char *out, size_t cap, const char *dir, const struct tm *tm, unsigned seq);

/** Plausibilitaet der Systemzeit: SNTP hat sie gestellt (alles vor 2024-01-01 gilt als "nicht gestellt"). */
int irm_time_is_valid(time_t t);

#ifdef __cplusplus
}
#endif
