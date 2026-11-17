/* Host-Test: WAV-Kopf und Aufnahme-Dateinamen. gcc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_wav.c test/test_wav.c -o /tmp/tw && /tmp/tw */
#include <stdio.h>
#include <string.h>
#include "irm_wav.h"

static int fails;
#define CHECK(c, ...) do { int ok_ = !!(c); printf("  %s ", ok_ ? "ok  " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

int main(void)
{
    printf("== WAV-Kopf 48 kHz Stereo 16 Bit, 1 s\n");
    uint8_t h[IRM_WAV_HDR_SIZE];
    irm_wav_header(h, 48000, 2, 16, 192000);
    CHECK(!memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVEfmt ", 8) && !memcmp(h + 36, "data", 4), "Chunk-Kennungen");
    CHECK(rd32(h + 4) == 192036, "RIFF-Groesse = data + 36 (%u)", (unsigned)rd32(h + 4));
    CHECK(rd32(h + 16) == 16 && rd16(h + 20) == 1, "fmt-Chunk 16 Byte, PCM");
    CHECK(rd16(h + 22) == 2 && rd32(h + 24) == 48000, "Kanaele/Rate");
    CHECK(rd32(h + 28) == 192000 && rd16(h + 32) == 4 && rd16(h + 34) == 16, "Byterate/Blockgroesse/Bits");
    CHECK(rd32(h + 40) == 192000, "data-Groesse");

    printf("== WAV-Kopf Kiwi 12 kHz Mono\n");
    irm_wav_header(h, 12000, 1, 16, 0);
    CHECK(rd32(h + 28) == 24000 && rd16(h + 32) == 2 && rd32(h + 4) == 36 && rd32(h + 40) == 0, "Byterate 24000, leere Datei gueltig");

    printf("== Begrenzung auf 32-Bit-Grenze\n");
    irm_wav_header(h, 48000, 2, 16, 0xFFFFFFFFu);
    CHECK(rd32(h + 40) == IRM_WAV_DATA_MAX && rd32(h + 4) == 0xFFFFFFFFu, "data=%u, RIFF=%u", (unsigned)rd32(h + 40), (unsigned)rd32(h + 4));

    printf("== Dateinamen\n");
    struct tm tm = { .tm_year = 126, .tm_mon = 10, .tm_mday = 16, .tm_hour = 14, .tm_min = 5, .tm_sec = 9 };   /* 2026-11-16 14:05:09 */
    char n[96];
    size_t l = irm_rec_filename(n, sizeof(n), "/sdcard/recordings", &tm, 0);
    CHECK(l == strlen(n) && !strcmp(n, "/sdcard/recordings/2026-11-16_14-05-09.wav"), "%s", n);
    irm_rec_filename(n, sizeof(n), "/sdcard/recordings", &tm, 1);
    CHECK(!strcmp(n, "/sdcard/recordings/2026-11-16_14-05-09.wav"), "seq 1 = kein Suffix");
    irm_rec_filename(n, sizeof(n), "/sdcard/recordings", &tm, 2);
    CHECK(!strcmp(n, "/sdcard/recordings/2026-11-16_14-05-09_2.wav"), "seq 2: %s", n);
    irm_rec_filename(n, sizeof(n), "/sdcard/recordings", NULL, 0);
    CHECK(!strcmp(n, "/sdcard/recordings/rec_0001.wav"), "ohne Uhr: %s", n);
    irm_rec_filename(n, sizeof(n), "/sdcard/recordings", NULL, 42);
    CHECK(!strcmp(n, "/sdcard/recordings/rec_0042.wav"), "ohne Uhr, seq 42: %s", n);
    char s[20]; memset(s, 'x', sizeof(s));
    CHECK(irm_rec_filename(s, sizeof(s), "/sdcard/recordings", &tm, 0) == 0 && s[0] == 0, "zu kurzer Puffer -> leer");
    CHECK(irm_rec_filename(NULL, 10, "/x", &tm, 0) == 0 && irm_rec_filename(n, 0, "/x", &tm, 0) == 0 && irm_rec_filename(n, sizeof(n), NULL, &tm, 0) == 0, "NULL/0 abgefangen");

    printf("== Systemzeit plausibel\n");
    CHECK(!irm_time_is_valid(0) && !irm_time_is_valid(1000000) && irm_time_is_valid(1704067200) && irm_time_is_valid(1794000000), "Epoche 0 ungueltig, 2024+ gueltig");

    printf(fails ? "\n%d FEHLER\n" : "\nalle Tests bestanden\n", fails);
    return fails ? 1 : 0;
}
