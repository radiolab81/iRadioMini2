/* Reine PCM-Hilfsfunktionen (ohne IDF-Abhaengigkeit, host-testbar). 16-Bit-Stereo, interleaved. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define IRM_PCM_UNITY 32768u   /* Q15: 1.0 */

/** Lautstaerke 0..100 % -> Verstaerkung (quadratische Kennlinie: 50 % ~ -12 dB, 10 % ~ -40 dB, 0 = stumm). */
uint32_t irm_pcm_gain_from_percent(uint8_t percent);
/** dst = clip(src * gain), n_samples einzelne Abtastwerte. dst == src erlaubt. */
void irm_pcm_scale(int16_t *dst, const int16_t *src, size_t n_samples, uint32_t gain_q15);
/** Stereo -> Mono: dst[i] = (L+R)/2 * gain. */
void irm_pcm_downmix_mono(int16_t *dst, const int16_t *src_stereo, size_t frames, uint32_t gain_q15);
/** 16 Bit vorzeichenbehaftet -> 8 Bit vorzeichenlos (Mitte 128) mit dreieckverteiltem Dither (verhindert Quantisierungsklirren bei leisen Passagen). */
void irm_pcm_to_u8(uint8_t *dst, const int16_t *src, size_t n_samples, uint32_t *dither_state);
#ifdef __cplusplus
}
#endif
