#include "irm_pcm.h"

uint32_t irm_pcm_gain_from_percent(uint8_t percent)
{
    if (percent > 100) percent = 100;
    return ((uint32_t)percent * percent * IRM_PCM_UNITY) / 10000u;
}

static inline int16_t clip16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }

void irm_pcm_scale(int16_t *dst, const int16_t *src, size_t n, uint32_t gain)
{
    for (size_t i = 0; i < n; i++) dst[i] = clip16(((int32_t)src[i] * (int32_t)gain) >> 15);
}

void irm_pcm_downmix_mono(int16_t *dst, const int16_t *src, size_t frames, uint32_t gain)
{
    for (size_t i = 0; i < frames; i++) {
        int32_t m = ((int32_t)src[2 * i] + (int32_t)src[2 * i + 1]) / 2;
        dst[i] = clip16((m * (int32_t)gain) >> 15);
    }
}

void irm_pcm_to_u8(uint8_t *dst, const int16_t *src, size_t n, uint32_t *state)
{
    uint32_t s = state ? *state : 1u;
    for (size_t i = 0; i < n; i++) {
        s = s * 1664525u + 1013904223u;               /* LCG */
        int32_t r1 = (int32_t)((s >> 16) & 0xFF);
        s = s * 1664525u + 1013904223u;
        int32_t r2 = (int32_t)((s >> 16) & 0xFF);
        int32_t v = (int32_t)src[i] + (r1 - r2);        /* TPDF-Dither, +-1 LSB des 8-Bit-Ziels */
        v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
        dst[i] = (uint8_t)((v + 32768) >> 8);
    }
    if (state) *state = s;
}
