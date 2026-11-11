/* Host-Test: Aufruf siehe README, Abschnitt Tests. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irm_resamp.h"

#define UNITY 32768u
#define PI_ 3.14159265358979323846

/* Sinus-Fit (Kleinste Quadrate auf sin/cos/Offset) -> Verhaeltnis Signal/Rest in dB. skip = Einschwingen am Anfang. */
static double snr_db(const int16_t *stereo, size_t frames, double f, double fs, size_t skip, double *amp_out)
{
    double sxx = 0, sxy = 0, ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, n = 0;
    for (size_t i = skip; i < frames; i++) {
        double t = (double)i / fs, s = sin(2 * PI_ * f * t), c = cos(2 * PI_ * f * t), y = stereo[2 * i];
        ss += s * s; cc += c * c; sc += s * c; ys += y * s; yc += y * c; n++; (void)sxx; (void)sxy;
    }
    double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double sig = 0, err = 0;
    for (size_t i = skip; i < frames; i++) {
        double t = (double)i / fs, m = a * sin(2 * PI_ * f * t) + b * cos(2 * PI_ * f * t), y = stereo[2 * i];
        sig += m * m; err += (y - m) * (y - m);
    }
    if (amp_out) *amp_out = sqrt(a * a + b * b);
    (void)n;
    return 10 * log10(sig / (err > 0 ? err : 1e-9));
}

static int16_t *tone(size_t n, double f, double fs, double amp)
{
    int16_t *x = malloc(n * sizeof *x);
    for (size_t i = 0; i < n; i++) x[i] = (int16_t)lrint(amp * sin(2 * PI_ * f * (double)i / fs));
    return x;
}

static uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }


/* ---- Simulation des Jitterpuffers: Quelle liefert 512-Sample-Frames mit Netz-Jitter (0..80 ms, nie vor dem Vorgaenger), Senke verbraucht
 * in 128-Sample-Schritten genau im Takt der Ausgaberate. Der Resampler ist (wie im Betrieb) auf die ANGENOMMENE Quellrate eingestellt;
 * die tatsaechliche weicht um 'drift' ab. Wiedergabe startet mit dem Soll-Fuellstand (Prebuffer). */
#define SIM_TARGET_MS 400
#define SIM_CAP_MS    1000
typedef struct { int under, over; double min_ms, max_ms; } sim_t;

static sim_t run_sim(double src_true, int use_ctrl)
{
    const double DST = 48000.0, SRC_ASSUMED = 11998.895, T_END = 1200.0;
    const double target = SIM_TARGET_MS / 1000.0 * src_true;
    irm_resamp_t rs; irm_resamp_init(&rs, 11998895, 48000);
    sim_t res = { 0, 0, 1e9, -1e9 };
    uint32_t seed = 4711;
    int16_t in[128] = { 0 }; static int16_t out[2 * 1024];
    int32_t avg_q8 = SIM_TARGET_MS << 8;
    double t = 0, consumed = 0, frames_in = 0;           /* frames_in: bis jetzt eingetroffene Frames */
    double last_arrival = 0, next_arrival;
    {   double j = (lcg(&seed) % 80000) / 1e6; next_arrival = 512.0 / src_true + j; last_arrival = next_arrival; }
    double k = 1;                                         /* Index des naechsten Frames */
    while (t < T_END) {
        while (next_arrival <= t) {
            frames_in++; k++;
            double a = k * 512.0 / src_true + (lcg(&seed) % 80000) / 1e6;
            if (a < last_arrival) a = last_arrival;
            last_arrival = next_arrival = a;
        }
        double fill = target + frames_in * 512.0 - consumed;           /* Samples im Puffer */
        if (fill < 128) {                                                /* Unterlauf: wie im Betrieb neu vorpuffern, solange steht der Verbrauch */
            res.under++;
            t += (target - fill) / src_true;
            avg_q8 = SIM_TARGET_MS << 8;
            continue;
        }
        double fill_ms = fill / src_true * 1000.0;
        if (fill_ms > SIM_CAP_MS) { res.over++; consumed += fill - target; }   /* Ueberlauf: ueberzaehliges wird verworfen */
        int32_t avg = irm_resamp_avg_update(&avg_q8, (int32_t)fill_ms);
        if (use_ctrl) irm_resamp_set_trim_ppm(&rs, irm_resamp_trim_ppm(avg - SIM_TARGET_MS));
        size_t used, o = irm_resamp_mono_to_stereo(&rs, in, 128, &used, out, 1024, UNITY);
        consumed += (double)used;
        t += (double)o / DST;
        if (t > 600) { if (avg < res.min_ms) res.min_ms = avg; if (avg > res.max_ms) res.max_ms = avg; }
    }
    (void)SRC_ASSUMED;
    return res;
}

int main(void)
{
    /* ---- 1) Tonqualitaet 12 kHz -> 48 kHz ---- */
    {
        const size_t N = 12000;   /* 1 s */
        irm_resamp_t r; irm_resamp_init(&r, 12000000, 48000);
        for (int k = 0; k < 3; k++) {
            const double f = k == 0 ? 440 : (k == 1 ? 1000 : 3000);
            int16_t *in = tone(N, f, 12000.0, 12000);
            size_t cap = irm_resamp_max_frames(12000000, 48000, N), used = 0;
            int16_t *out = malloc(cap * 4);
            size_t o = irm_resamp_mono_to_stereo(&r, in, N, &used, out, cap, UNITY);
            assert(used == N);
            double amp, snr = snr_db(out, o, f, 48000, 200, &amp);
            printf("  %4.0f Hz: %zu Frames, Amplitude %.0f (Soll 12000), Signal/Rest %.1f dB\n", f, o, amp, snr);
            if (!(fabs(amp - 12000) < 120)) { fflush(stdout); }
            assert(fabs(amp - 12000) < 120);
            assert(snr > (k == 2 ? 36 : 50));                    /* Interpolationsfehler; 3 kHz liegt bei nur 4 Samples/Periode */
            for (size_t i = 0; i < o; i++) assert(out[2 * i] == out[2 * i + 1]);
            free(in); free(out);
            irm_resamp_init(&r, 12000000, 48000);
        }
    }

    /* ---- 2) Blockgrenzen: beliebige Aufteilung der Eingabe liefert bitgleiche Ausgabe ---- */
    {
        const size_t N = 5000;
        int16_t *in = tone(N, 777, 11998.895, 9000);
        size_t cap = irm_resamp_max_frames(11998895, 44100, N) + 16;
        int16_t *a = malloc(cap * 4), *b = malloc(cap * 4);
        irm_resamp_t r1, r2; irm_resamp_init(&r1, 11998895, 44100); irm_resamp_init(&r2, 11998895, 44100);
        size_t used, na = irm_resamp_mono_to_stereo(&r1, in, N, &used, a, cap, 40000);
        assert(used == N);
        size_t nb = 0, pos = 0; uint32_t seed = 7;
        while (pos < N) {
            size_t chunk = 1 + lcg(&seed) % 200, ocap = 1 + lcg(&seed) % 50;     /* auch winzige Ausgabepuffer: dann weiter mit Rest */
            if (chunk > N - pos) chunk = N - pos;
            size_t u = 0, o = irm_resamp_mono_to_stereo(&r2, in + pos, chunk, &u, b + 2 * nb, ocap, 40000);
            nb += o; pos += u;
            if (u == 0 && o == 0) { fprintf(stderr, "Stillstand\n"); return 1; }
        }
        while (nb < na) { size_t u, o = irm_resamp_mono_to_stereo(&r2, in, 0, &u, b + 2 * nb, 1 + lcg(&seed) % 9, 40000); if (!o) break; nb += o; }
        assert(nb == na && memcmp(a, b, na * 4) == 0);
        printf("  Blockgrenzen: %zu Frames bitgleich (Ausgabepuffer 1..50 Frames)\n", na);
        free(in); free(a); free(b);
    }

    /* ---- 3) Frame-Zahl = Verhaeltnis der Raten (inkl. 44,1 kHz und der echten Kiwi-Rate) ---- */
    {
        static const struct { uint32_t src_mhz, dst; } c[] = { { 12000000, 48000 }, { 11998895, 48000 }, { 12000000, 44100 }, { 11998895, 44100 }, { 12000000, 32000 } };
        for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
            irm_resamp_t r; irm_resamp_init(&r, c[k].src_mhz, c[k].dst);
            const size_t N = 600000;     /* 50 s */
            int16_t *in = calloc(N, 2); size_t cap = irm_resamp_max_frames(c[k].src_mhz, c[k].dst, N), used;
            int16_t *out = malloc(cap * 4);
            size_t o = irm_resamp_mono_to_stereo(&r, in, N, &used, out, cap, UNITY);
            double want = (double)N * c[k].dst / (c[k].src_mhz / 1000.0);
            printf("  %.3f Hz -> %u Hz: %zu Frames (Soll %.2f)\n", c[k].src_mhz / 1000.0, c[k].dst, o, want);
            if (!(used == N && fabs((double)o - want) < 6)) { printf("  FEHLER: used=%zu o=%zu cap=%zu\n", used, o, cap); fflush(stdout); }
            assert(used == N && fabs((double)o - want) < 6);
            free(in); free(out);
        }
    }

    /* ---- 4) Verstaerkung und Begrenzung ---- */
    {
        int16_t in[64]; for (int i = 0; i < 64; i++) in[i] = (i & 1) ? -20000 : 20000;   /* Nyquist-Rechteck */
        int16_t out[2 * 512]; size_t used;
        irm_resamp_t r; irm_resamp_init(&r, 12000000, 12000);                          /* 1:1 */
        size_t o = irm_resamp_mono_to_stereo(&r, in, 64, &used, out, 512, 2 * UNITY);   /* Faktor 2 */
        int clipped = 0; for (size_t i = 0; i < o; i++) { assert(out[2*i] >= -32768 && out[2*i] <= 32767); clipped += (out[2*i] == 32767 || out[2*i] == -32768); }
        assert(clipped > 10);
        irm_resamp_init(&r, 12000000, 12000);
        o = irm_resamp_mono_to_stereo(&r, in, 64, &used, out, 512, 0);                  /* stumm */
        for (size_t i = 0; i < 2 * o; i++) assert(out[i] == 0);
        puts("  Verstaerkung/Begrenzung ok");
    }

    /* ---- 5) Trim: +-500 ppm veraendert die Frame-Zahl um 500 ppm ---- */
    {
        const size_t N = 1200000;   /* 100 s */
        int16_t *in = calloc(N, 2), *out = malloc((size_t)(N * 4.01) * 4); size_t used;
        size_t cnt[3];
        for (int k = 0; k < 3; k++) {
            irm_resamp_t r; irm_resamp_init(&r, 12000000, 48000); irm_resamp_set_trim_ppm(&r, (k - 1) * 500);
            cnt[k] = irm_resamp_mono_to_stereo(&r, in, N, &used, out, (size_t)(N * 4.01), UNITY);
        }
        double d_up = ((double)cnt[1] - (double)cnt[2]) / (double)cnt[1] * 1e6, d_dn = ((double)cnt[0] - (double)cnt[1]) / (double)cnt[1] * 1e6;
        printf("  Trim: -500 ppm -> %+.0f ppm Frames, +500 ppm -> %+.0f ppm Frames\n", d_dn, -d_up);
        assert(fabs(d_up - 500) < 5 && fabs(d_dn - 500) < 5);   /* mehr Verbrauch = weniger Ausgabe, und umgekehrt */
        irm_resamp_t r; irm_resamp_init(&r, 12000000, 48000); irm_resamp_set_trim_ppm(&r, 5000);
        irm_resamp_t q; irm_resamp_init(&q, 12000000, 48000); irm_resamp_set_trim_ppm(&q, IRM_RESAMP_TRIM_MAX_PPM);
        assert(r.step == q.step);   /* Begrenzung */
        free(in); free(out);
    }

    /* ---- 6) Obergrenze der Ausgabeframes wird nie erreicht ueberschritten ---- */
    {
        irm_resamp_t r; irm_resamp_init(&r, 11998895, 48000); irm_resamp_set_trim_ppm(&r, -1000);
        int16_t in[128] = {0}; static int16_t out[2 * 2048]; size_t cap = irm_resamp_max_frames(11998895, 48000, 128);
        assert(cap <= 2048);
        for (int i = 0; i < 20000; i++) { size_t u; size_t o = irm_resamp_mono_to_stereo(&r, in, 128, &u, out, 1 << 20 > 2048 ? 2048 : 0, UNITY); assert(u == 128 && o <= cap); }
        puts("  max_frames-Schranke ok");
    }

    /* ---- 7) Regler: Fuellstand bleibt ueber 20 min mit Drift und Jitter im Soll ---- */
    {
        static const double drift_ppm[3] = { 0, -400, +400 };
        static const char *const name[3] = { "Takte exakt gleich", "Quelle 400 ppm langsamer", "Quelle 400 ppm schneller" };
        for (int use_ctrl = 0; use_ctrl <= 1; use_ctrl++) {
            for (int sc = 0; sc < 3; sc++) {
                if (!use_ctrl && sc != 1) continue;    /* Gegenprobe ohne Regler nur fuer die langsame Quelle */
                sim_t r = run_sim(11998.895 * (1 + drift_ppm[sc] * 1e-6), use_ctrl);
                printf("  %-26s Regler %s: Unterlaeufe %d, Ueberlaeufe %d, Fuellstand ab 10 min %.0f..%.0f ms (Soll %d)\n",
                       name[sc], use_ctrl ? "AN " : "AUS", r.under, r.over, r.min_ms, r.max_ms, SIM_TARGET_MS);
                if (use_ctrl) { assert(r.under == 0 && r.over == 0); assert(r.min_ms > 250 && r.max_ms < 600); }
                else if (sc == 1) assert(r.under > 0);   /* Gegenprobe: ohne Regler laeuft der Puffer leer, die Simulation bildet die Drift also ab */
            }
        }
    }

    puts("test_resamp: OK");
    return 0;
}
