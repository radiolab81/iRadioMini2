#include <stdio.h>
#include <string.h>
#include "irm_wav.h"

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

void irm_wav_header(uint8_t out[IRM_WAV_HDR_SIZE], uint32_t sample_rate, uint16_t channels, uint16_t bits, uint32_t data_bytes)
{
    if (data_bytes > IRM_WAV_DATA_MAX) data_bytes = IRM_WAV_DATA_MAX;
    const uint32_t block = (uint32_t)channels * ((uint32_t)bits / 8u);
    memcpy(out, "RIFF", 4);          le32(out + 4, 36u + data_bytes);
    memcpy(out + 8, "WAVEfmt ", 8);  le32(out + 16, 16);          /* fmt-Chunk: 16 Byte */
    le16(out + 20, 1);                                            /* PCM */
    le16(out + 22, channels);        le32(out + 24, sample_rate);
    le32(out + 28, sample_rate * block);                          /* Byterate */
    le16(out + 32, block);           le16(out + 34, bits);
    memcpy(out + 36, "data", 4);     le32(out + 40, data_bytes);
}

size_t irm_rec_filename(char *out, size_t cap, const char *dir, const struct tm *tm, unsigned seq)
{
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!dir) return 0;
    int n;
    if (tm) {
        if (seq >= 2) n = snprintf(out, cap, "%s/%04d-%02d-%02d_%02d-%02d-%02d_%u.wav", dir, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                                   tm->tm_hour, tm->tm_min, tm->tm_sec, seq);
        else   n = snprintf(out, cap, "%s/%04d-%02d-%02d_%02d-%02d-%02d.wav", dir, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                            tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        n = snprintf(out, cap, "%s/rec_%04u.wav", dir, seq ? seq : 1u);
    }
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return 0; }
    return (size_t)n;
}

int irm_time_is_valid(time_t t) { return t >= (time_t)1704067200; }   /* 2024-01-01 00:00:00 UTC */
