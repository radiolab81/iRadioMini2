#include "irm_resamp.h"
#include <math.h>
#include <string.h>

#define ONE (1ULL << 32)

/* ---- Koeffiziententabelle: tab[p][k] = Kernel(d = j - p/PHASES), j = k - (TAPS/2 - 1), Kernel = sinc(d) * Kaiser(d/(TAPS/2)).
 * Jede Phase wird auf die Summe 1,0 (Q14, damit 1,0 in int16_t passt) normiert -> Gleichanteil exakt 1. Wird beim ersten Init berechnet (einmal, ~1600 Auswertungen). */
#define PHASES      64
#define PHASE_BITS  6
#define KAISER_BETA 6.8f
static int16_t s_tab[PHASES + 1][IRM_RESAMP_TAPS];
static volatile int s_tab_ready;

static float bessel_i0(float x)
{
    float sum = 1.0f, term = 1.0f;
    for (int k = 1; k < 40; k++) {
        float h = x / (2.0f * (float)k);
        term *= h * h;
        sum += term;
        if (term < 1e-9f * sum) break;
    }
    return sum;
}

static void build_table(void)
{
    const float pi = 3.14159265358979f, i0b = bessel_i0(KAISER_BETA);
    for (int p = 0; p <= PHASES; p++) {
        float row[IRM_RESAMP_TAPS], sum = 0;
        const float frac = (float)p / (float)PHASES;
        for (int k = 0; k < IRM_RESAMP_TAPS; k++) {
            const float d = (float)(k - (IRM_RESAMP_TAPS / 2 - 1)) - frac;
            const float x = d / (float)(IRM_RESAMP_TAPS / 2);
            const float sinc = (d > -1e-6f && d < 1e-6f) ? 1.0f : sinf(pi * d) / (pi * d);
            const float win = (x > -1.0f && x < 1.0f) ? bessel_i0(KAISER_BETA * sqrtf(1.0f - x * x)) / i0b : 0.0f;
            row[k] = sinc * win;
            sum += row[k];
        }
        for (int k = 0; k < IRM_RESAMP_TAPS; k++) s_tab[p][k] = (int16_t)lrintf(row[k] / sum * 16384.0f);
    }
    s_tab_ready = 1;
}

void irm_resamp_init(irm_resamp_t *r, uint32_t src_rate_mhz, uint32_t dst_rate_hz)
{
    if (!s_tab_ready) build_table();   /* idempotent; ein gleichzeitiger zweiter Aufruf schreibt identische Werte */
    memset(r, 0, sizeof(*r));
    if (dst_rate_hz == 0) dst_rate_hz = 1;
    r->step_nom = ((uint64_t)src_rate_mhz << 32) / ((uint64_t)dst_rate_hz * 1000u);
    r->step = r->step_nom;
}

void irm_resamp_set_trim_ppm(irm_resamp_t *r, int32_t ppm)
{
    if (ppm > IRM_RESAMP_TRIM_MAX_PPM) ppm = IRM_RESAMP_TRIM_MAX_PPM;
    if (ppm < -IRM_RESAMP_TRIM_MAX_PPM) ppm = -IRM_RESAMP_TRIM_MAX_PPM;
    r->step = (uint64_t)((int64_t)r->step_nom + ((int64_t)r->step_nom * ppm) / 1000000);
}

/* Ein Ausgabesample an der aktuellen Phase: Koeffizienten der Nachbarphasen linear mischen (Q10), dann 24 Multiplikationen.
 * Betragssumme der Koeffizienten < 1,4 -> der int32-Akkumulator laeuft bei 16-Bit-Eingang nicht ueber (1,4 * 2^14 * 2^15 < 2^31). */
static int32_t filter(const irm_resamp_t *r)
{
    const uint32_t ph = (uint32_t)(r->frac >> (32 - PHASE_BITS));          /* 0..63 */
    const int32_t  rem = (int32_t)((r->frac >> (32 - PHASE_BITS - 10)) & 0x3FF);
    const int16_t *c0 = s_tab[ph], *c1 = s_tab[ph + 1];
    int32_t acc = 0;
    for (int k = 0; k < IRM_RESAMP_TAPS; k++) {
        const int32_t c = c0[k] + (((int32_t)c1[k] - c0[k]) * rem >> 10);
        acc += c * r->w[k];
    }
    return acc >> 14;
}

size_t irm_resamp_mono_to_stereo(irm_resamp_t *r, const int16_t *in, size_t n_in, size_t *used,
                                 int16_t *out, size_t cap, uint32_t gain_q15)
{
    size_t i = 0, o = 0;
    for (;;) {
        while (r->frac >= ONE) {
            if (i >= n_in) goto done;
            memmove(r->w, r->w + 1, (IRM_RESAMP_TAPS - 1) * sizeof(r->w[0]));
            r->w[IRM_RESAMP_TAPS - 1] = in[i++];
            r->frac -= ONE;
        }
        if (o >= cap) break;
        int64_t v = ((int64_t)filter(r) * (int64_t)gain_q15) >> 15;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[2 * o] = out[2 * o + 1] = (int16_t)v;
        o++;
        r->frac += r->step;
    }
done:
    if (used) *used = i;
    return o;
}

size_t irm_resamp_max_frames(uint32_t src_rate_mhz, uint32_t dst_rate_hz, size_t n_in)
{
    if (src_rate_mhz == 0) src_rate_mhz = 1;
    /* n_in * dst/src, +0,2 % Trim-Reserve, +4 Rundung/Anlauf */
    uint64_t f = ((uint64_t)n_in * dst_rate_hz * 1000u) / src_rate_mhz;
    return (size_t)(f + f / 500u + 4u);
}

int32_t irm_resamp_trim_ppm(int32_t fill_err_ms)
{
    int32_t p = fill_err_ms * IRM_RESAMP_TRIM_PPM_PER_MS;
    if (p > IRM_RESAMP_TRIM_MAX_PPM) p = IRM_RESAMP_TRIM_MAX_PPM;
    if (p < -IRM_RESAMP_TRIM_MAX_PPM) p = -IRM_RESAMP_TRIM_MAX_PPM;
    return p;
}

int32_t irm_resamp_avg_update(int32_t *avg_q8, int32_t fill_ms)
{
    *avg_q8 += ((fill_ms << 8) - *avg_q8) / 256;
    return (*avg_q8 + 128) >> 8;
}
