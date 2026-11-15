/* Host-Test Stereo-Resampler: gcc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_resamp.c test/test_resamp_st.c -lm -o /tmp/trs && /tmp/trs */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irm_resamp.h"

static int fails;
#define CHECK(c, ...) do { if (c) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } else { printf("  FEHLER "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void tone(int16_t *st, size_t n, double rate, double fl, double fr, double amp)
{
    for (size_t i = 0; i < n; i++) {
        st[2 * i]     = (int16_t)lrint(amp * sin(2 * M_PI * fl * (double)i / rate));
        st[2 * i + 1] = (int16_t)lrint(amp * sin(2 * M_PI * fr * (double)i / rate));
    }
}
/* Amplitude einer Frequenz im Kanal ch (Einzelbin-DFT ueber den eingeschwungenen Teil) */
static double amp_at(const int16_t *st, size_t n, int ch, double rate, double f)
{
    double re = 0, im = 0; size_t a = 200, cnt = 0;
    for (size_t i = a; i + 200 < n; i++, cnt++) { const double ph = 2 * M_PI * f * (double)i / rate; re += st[2 * i + ch] * cos(ph); im -= st[2 * i + ch] * sin(ph); }
    return 2.0 * sqrt(re * re + im * im) / (double)cnt;
}
static size_t run(irm_resamp_st_t *r, const int16_t *in, size_t n, int16_t *out, size_t cap, size_t chunk, uint32_t gain)
{
    size_t o = 0, off = 0;
    while (off < n) {
        const size_t take = (n - off) < chunk ? (n - off) : chunk;
        size_t used = 0, done = 0;
        while (done < take) {
            const size_t k = irm_resamp_st_process(r, in + 2 * (off + done), take - done, &used, out + 2 * o, cap - o, gain);
            o += k; done += used;
            if (!used && !k) break;
        }
        off += take;
    }
    return o;
}
static double db(double a, double b) { return 20.0 * log10(a / b); }

int main(void)
{
    const size_t N = 48000;
    int16_t *in = malloc(N * 4), *out = malloc((N * 2 + 100) * 4), *out2 = malloc((N * 2 + 100) * 4);
    static irm_resamp_tab_t lp;
    irm_resamp_st_t r;

    printf("== 48 kHz -> 48 kHz (nur Driftregelung)\n");
    tone(in, N, 48000, 1000, 3000, 10000);
    irm_resamp_st_init(&r, NULL, 48000000, 48000);
    size_t o = run(&r, in, N, out, N + 50, 480, 32768);
    CHECK(o >= N - 2 && o <= N + 2, "Frames: %zu fuer %zu Eingangsframes (1:1, erste Ausgabe entsteht ohne Eingabe)", o, N);
    CHECK(fabs(db(amp_at(out, o, 0, 48000, 1000), 10000)) < 0.1 && fabs(db(amp_at(out, o, 1, 48000, 3000), 10000)) < 0.1, "L 1 kHz / R 3 kHz bleiben unveraendert (+-0,1 dB)");
    CHECK(amp_at(out, o, 0, 48000, 3000) < 30 && amp_at(out, o, 1, 48000, 1000) < 30, "keine Kanalmischung (Uebersprechen < 30 LSB)");
    tone(in, N, 48000, 15000, 18000, 10000);
    irm_resamp_st_init(&r, NULL, 48000000, 48000);
    o = run(&r, in, N, out, N + 50, 480, 32768);
    CHECK(fabs(db(amp_at(out, o, 0, 48000, 15000), 10000)) < 0.3, "15 kHz: %+.2f dB", db(amp_at(out, o, 0, 48000, 15000), 10000));

    printf("== Blockgroesse aendert das Ergebnis nicht\n");
    tone(in, N, 48000, 1000, 3000, 10000);
    irm_resamp_st_init(&r, NULL, 48000000, 48000);
    o = run(&r, in, N, out, N + 50, N, 32768);
    irm_resamp_st_init(&r, NULL, 48000000, 48000);
    size_t o2 = run(&r, in, N, out2, N + 50, 37, 32768);
    CHECK(o == o2 && memcmp(out, out2, o * 4) == 0, "Block 48000 == Block 37 (%zu Frames, bitgleich)", o);

    printf("== Driftregelung (Trim)\n");
    irm_resamp_st_init(&r, NULL, 48000000, 48000); irm_resamp_st_set_trim_ppm(&r, 1000);
    const size_t op = run(&r, in, N, out, N + 100, 480, 32768);
    irm_resamp_st_init(&r, NULL, 48000000, 48000); irm_resamp_st_set_trim_ppm(&r, -1000);
    const size_t om = run(&r, in, N, out, N + 100, 480, 32768);
    CHECK(om > op && (om - op) >= 90 && (om - op) <= 100, "+-1000 ppm: %zu vs %zu Frames (Differenz %zu, erwartet ~96)", op, om, om - op);
    irm_resamp_st_init(&r, NULL, 48000000, 48000); irm_resamp_st_set_trim_ppm(&r, 1000000);
    CHECK(r.step <= r.step_nom + r.step_nom / 999, "Trim wird auf +-1000 ppm begrenzt");

    printf("== 48 kHz -> 44,1 kHz (Abwaertsabtastung mit Tiefpass)\n");
    tone(in, N, 48000, 1000, 3000, 10000);
    irm_resamp_st_init(&r, &lp, 48000000, 44100);
    o = run(&r, in, N, out, 44200, 480, 32768);
    CHECK(o >= 44100 - 30 && o <= 44100 + 2, "Frames: %zu (erwartet ~44100)", o);
    CHECK(fabs(db(amp_at(out, o, 0, 44100, 1000), 10000)) < 0.1 && fabs(db(amp_at(out, o, 1, 44100, 3000), 10000)) < 0.1, "1 kHz / 3 kHz: %+.3f / %+.3f dB", db(amp_at(out, o, 0, 44100, 1000), 10000), db(amp_at(out, o, 1, 44100, 3000), 10000));
    tone(in, N, 48000, 15000, 20000, 10000);
    irm_resamp_st_init(&r, &lp, 48000000, 44100);
    o = run(&r, in, N, out, 44200, 480, 32768);
    const double a15 = db(amp_at(out, o, 0, 44100, 15000), 10000), a20 = db(amp_at(out, o, 1, 44100, 20000), 10000);
    CHECK(a15 > -0.5, "15 kHz Durchlassbereich: %+.2f dB", a15);
    printf("       Info: 20 kHz (Uebergangsbereich): %+.2f dB\n", a20);
    /* 23,5 kHz liegt oberhalb der halben Zielrate (22,05 kHz) -> wuerde bei 44,1 kHz auf 20,6 kHz zurueckfalten */
    tone(in, N, 48000, 23500, 23500, 10000);
    irm_resamp_st_init(&r, &lp, 48000000, 44100);
    o = run(&r, in, N, out, 44200, 480, 32768);
    const double alias = db(amp_at(out, o, 0, 44100, 44100 - 23500), 10000);
    printf("       Info: 23,5 kHz -> Aliasanteil bei 20,6 kHz: %+.1f dB\n", alias);
    CHECK(alias < -12, "Alias-Daempfung mit Tiefpass: %.1f dB", alias);
    irm_resamp_st_init(&r, NULL, 48000000, 44100);    /* ohne Tiefpass-Speicher: Vergleichswert */
    o = run(&r, in, N, out, 44200, 480, 32768);
    const double alias0 = db(amp_at(out, o, 0, 44100, 44100 - 23500), 10000);
    printf("       Info: ohne Tiefpass: %+.1f dB\n", alias0);
    CHECK(alias < alias0 - 6, "Tiefpass daempft den Alias deutlich mehr (%.1f vs %.1f dB)", alias, alias0);

    printf("== 32 kHz -> 48 kHz (Aufwaertsabtastung)\n");
    tone(in, N, 32000, 1000, 5000, 10000);
    irm_resamp_st_init(&r, &lp, 32000000, 48000);
    o = run(&r, in, N, out, 72200, 320, 32768);
    CHECK(o >= 72000 - 40 && o <= 72000 + 2, "Frames: %zu (erwartet ~72000)", o);
    CHECK(fabs(db(amp_at(out, o, 0, 48000, 1000), 10000)) < 0.1 && fabs(db(amp_at(out, o, 1, 48000, 5000), 10000)) < 0.1, "1 kHz / 5 kHz bleiben erhalten");

    printf("== Verstaerkung und Begrenzung\n");
    tone(in, N, 48000, 1000, 3000, 20000);
    irm_resamp_st_init(&r, NULL, 48000000, 48000);
    o = run(&r, in, N, out, N + 50, 480, 16384);
    CHECK(fabs(db(amp_at(out, o, 0, 48000, 1000), 10000)) < 0.1, "gain 0,5 halbiert 20000 -> 10000");
    irm_resamp_st_init(&r, NULL, 48000000, 48000);
    o = run(&r, in, N, out, N + 50, 480, 65536);
    int16_t mx = 0; for (size_t i = 0; i < o * 2; i++) if (out[i] > mx) mx = out[i];
    CHECK(mx == 32767, "gain 2,0 begrenzt hart bei 32767 (max %d)", mx);

    free(in); free(out); free(out2);
    printf("== Ergebnis: %s\n", fails ? "FEHLER" : "OK");
    return fails ? 1 : 0;
}
