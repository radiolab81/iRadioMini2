/* Host-Test des ECHTEN irm_rec.c (POSIX-Shim statt FreeRTOS/IDF) gegen ein Verzeichnis als "SD-Karte".
 * Prueft: Abgriff Radio (Stereo, ueber sink->write) und Kiwi (Mono), Dateiinhalt bit-genau, WAV-Kopf, Dateiwechsel ohne Verlust und Frame-Versatz,
 * Puffer-Ueberlauf (nur ganze Bloecke verworfen), Karte-voll-Abbruch, nicht aufnehmbare Quellen. */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "irm_rec.h"
#include "irm_core.h"
#include "esp_vfs_fat.h"
#include <pthread.h>

static int fails;
#define CHECK(c, ...) do { int ok_ = !!(c); printf("  %s ", ok_ ? "ok  " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

static irm_status_t g_st;
void irm_status_update(irm_status_mutator_t fn, void *ctx) { fn(&g_st, ctx); }
bool irm_board_sd_mounted(void) { return 1; }
static uint64_t g_free = 1ull << 40;
esp_err_t esp_vfs_fat_info(const char *b, uint64_t *t, uint64_t *f) { (void)b; *t = 1ull << 41; *f = g_free; return ESP_OK; }

static size_t sink_bytes;
static esp_err_t fake_write(irm_out_sink_t *s, const int16_t *p, size_t frames) { (void)s; (void)p; sink_bytes += frames * 4; return ESP_OK; }

/* aufsteigende Zaehlerwerte: jede Verschiebung oder Luecke faellt auf */
static uint16_t ctr;
static void gen(int16_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (int16_t)ctr++; }

static size_t list(char names[][64], size_t max)
{
    char d[128]; snprintf(d, sizeof(d), "%s/recordings", IRM_SD_MOUNT_POINT);
    DIR *dp = opendir(d); size_t n = 0; struct dirent *e;
    while (dp && (e = readdir(dp)) && n < max) if (e->d_name[0] != '.') strcpy(names[n++], e->d_name);
    if (dp) closedir(dp);
    for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) if (strcmp(names[i], names[j]) > 0) { char t[64]; strcpy(t, names[i]); strcpy(names[i], names[j]); strcpy(names[j], t); }
    return n;
}
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Datei lesen, Kopf pruefen; Nutzdaten (ohne Kopf) anhaengen. Rueckgabe: Nutzbytes oder -1 */
static long slurp(const char *name, uint32_t rate, uint16_t ch, uint8_t **acc, size_t *acc_n)
{
    char p[160]; snprintf(p, sizeof(p), "%s/recordings/%s", IRM_SD_MOUNT_POINT, name);
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)sz); fread(b, 1, (size_t)sz, f); fclose(f);
    int ok = sz >= 44 && !memcmp(b, "RIFF", 4) && !memcmp(b + 36, "data", 4) && le32(b + 24) == rate && b[22] == ch && le32(b + 40) == (uint32_t)(sz - 44) && le32(b + 4) == (uint32_t)(sz - 8);
    if (!ok) { printf("  Kopf von %s ungueltig (Groesse %ld, data %u)\n", name, sz, (unsigned)le32(b + 40)); free(b); return -1; }
    *acc = realloc(*acc, *acc_n + (size_t)sz - 44); memcpy(*acc + *acc_n, b + 44, (size_t)sz - 44); *acc_n += (size_t)sz - 44;
    free(b); return sz - 44;
}
static volatile int feeder_run;
static void *feeder(void *a) { (void)a; int16_t blk[128]; uint16_t c = 0; while (feeder_run) { for (int i = 0; i < 128; i++) blk[i] = (int16_t)c++; irm_rec_feed_kiwi(blk, 128, 12000); usleep(300); } return NULL; }
static void wipe(void) { char c[200]; snprintf(c, sizeof(c), "rm -rf %s/recordings", IRM_SD_MOUNT_POINT); if (system(c)) {} }
static void wait_active(int want) { for (int i = 0; i < 3000 && irm_rec_active() != (bool)want; i++) usleep(1000); }

int main(void)
{
    mkdir(IRM_SD_MOUNT_POINT, 0777); wipe();
    irm_out_sink_t sink = { .name = "fake", .write = fake_write, .sample_rate = 48000 };
    CHECK(irm_rec_init(&sink) == ESP_OK && sink.write != fake_write, "init haengt sich in sink->write ein");
    CHECK(irm_rec_start(IRM_SOURCE_MEDIA) == ESP_ERR_NOT_SUPPORTED && irm_rec_start(IRM_SOURCE_NET) == ESP_ERR_NOT_SUPPORTED, "Dateien/Netzwerk nicht aufnehmbar");
    CHECK(!irm_rec_active() && irm_rec_stop() == ESP_OK, "ohne Aufnahme: stop ist harmlos");

    printf("== Radio: 3 s Stereo 48 kHz ueber sink->write, Ton wird weiter ausgegeben\n");
    { int16_t blk[960 * 2]; ctr = 0; sink_bytes = 0;
      CHECK(irm_rec_start(IRM_SOURCE_RADIO) == ESP_OK && g_st.rec, "Start, Status rec=1");
      CHECK(irm_rec_start(IRM_SOURCE_RADIO) == ESP_ERR_INVALID_STATE, "zweiter Start abgelehnt");
      char n0[1][64]; CHECK(list(n0, 1) == 0, "vor dem ersten Ton keine Datei");
      for (int i = 0; i < 150; i++) { gen(blk, 960 * 2); sink.write(&sink, blk, 960); usleep(2000); }
      CHECK(irm_rec_stop() == ESP_OK && !irm_rec_active() && !g_st.rec, "Stop synchron, Status rec=0");
      CHECK(sink_bytes == 150u * 960 * 4, "Ausgabe unveraendert durchgereicht (%zu)", sink_bytes);
      char nm[8][64]; size_t n = list(nm, 8); uint8_t *acc = NULL; size_t an = 0;
      CHECK(n == 1 && strlen(nm[0]) == 23 && nm[0][4] == '-' && nm[0][10] == '_', "eine Datei mit Zeitstempelnamen: %s", n ? nm[0] : "-");
      long d = n ? slurp(nm[0], 48000, 2, &acc, &an) : -1;
      CHECK(d == 150L * 960 * 4, "Nutzdaten %ld Byte, Kopf stimmt", d);
      int ident = d > 0; for (size_t i = 0; ident && i < an / 2; i++) if (((uint16_t *)acc)[i] != (uint16_t)i) ident = 0;
      CHECK(ident, "Inhalt bit-genau, nichts verschoben"); free(acc);
      irm_rec_info_t ri; irm_rec_info(&ri); CHECK(ri.secs == 3 && ri.files == 1 && ri.dropped_bytes == 0 && !ri.end_reason[0], "Info: %u s, %u Datei, verworfen %u", (unsigned)ri.secs, (unsigned)ri.files, (unsigned)ri.dropped_bytes);
    }
    wipe();

    printf("== Kiwi: 12 kHz Mono; Senken-Ausgabe (Stereo) wird dabei NICHT aufgenommen\n");
    { int16_t blk[128]; int16_t st[256 * 2] = { 0 }; ctr = 0;
      CHECK(irm_rec_start(IRM_SOURCE_KIWI) == ESP_OK, "Start Kiwi");
      for (int i = 0; i < 100; i++) { gen(blk, 128); irm_rec_feed_kiwi(blk, 128, 12000); sink.write(&sink, st, 256); usleep(1000); }
      CHECK(irm_rec_stop() == ESP_OK, "Stop");
      char nm[8][64]; size_t n = list(nm, 8); uint8_t *acc = NULL; size_t an = 0;
      long d = n == 1 ? slurp(nm[0], 12000, 1, &acc, &an) : -1;
      CHECK(d == 100L * 128 * 2, "genau die Kiwi-Samples (%ld Byte), Mono 12 kHz", d);
      int ident = d > 0; for (size_t i = 0; ident && i < an / 2; i++) if (((uint16_t *)acc)[i] != (uint16_t)i) ident = 0;
      CHECK(ident, "Inhalt bit-genau"); free(acc);
    }
    wipe();

    printf("== Dateiwechsel bei 1 MiB (3,2 MiB Stereo): keine Luecke, kein Frame-Versatz\n");
    { int16_t blk[960 * 2]; ctr = 0; irm_rec_start(IRM_SOURCE_RADIO);
      for (int i = 0; i < 880; i++) { gen(blk, 960 * 2); sink.write(&sink, blk, 960); while (0) {} if (i % 8 == 0) usleep(1000); }
      for (int i = 0; i < 20 && irm_rec_active(); i++) usleep(1000);
      irm_rec_stop();
      char nm[16][64]; size_t n = list(nm, 16); uint8_t *acc = NULL; size_t an = 0; int allok = 1;
      for (size_t i = 0; i < n; i++) if (slurp(nm[i], 48000, 2, &acc, &an) < 0) allok = 0;
      irm_rec_info_t ri; irm_rec_info(&ri);
      CHECK(n >= 3 && allok, "%zu Dateien, alle Koepfe gueltig (Namen: %s, %s ...)", n, n ? nm[0] : "-", n > 1 ? nm[1] : "-");
      size_t ok_frames = an / 4, fed = 880u * 960;
      printf("  info: %zu von %zu Frames geschrieben, verworfen %u Byte\n", ok_frames, fed, (unsigned)ri.dropped_bytes);
      CHECK(an % 4 == 0 && ok_frames + ri.dropped_bytes / 4 == fed, "geschrieben + verworfen = geliefert");
      /* Verworfene sind ganze Bloecke -> Zaehler steigt dort sprunghaft; innerhalb der Datei muss L immer geradzahlig sein (kein Links/Rechts-Tausch) */
      int swapped = 0; for (size_t i = 0; i < an / 2; i += 2) if (((uint16_t *)acc)[i] & 1) swapped = 1;
      CHECK(!swapped, "Links/Rechts nie vertauscht"); free(acc);
    }
    wipe();

    printf("== Start/Stopp unter Dauerlast (Kiwi-Feeder laeuft waehrend Stopp weiter): kein Use-after-free\n");
    { pthread_t th; feeder_run = 1; pthread_create(&th, NULL, feeder, NULL); int bad = 0;
      for (int i = 0; i < 60; i++) { irm_rec_start(IRM_SOURCE_KIWI); usleep(3000 + (i % 7) * 1500); if (irm_rec_stop() != ESP_OK) bad++; }
      feeder_run = 0; pthread_join(th, NULL);
      CHECK(!bad && !irm_rec_active(), "60 Zyklen ohne Haenger/Absturz (ASan sauber)"); }
    wipe();

    printf("== Karte fast voll: Aufnahme endet selbst\n");
    { g_free = 1ull << 20; irm_rec_start(IRM_SOURCE_RADIO); wait_active(0);
      irm_rec_info_t ri; irm_rec_info(&ri);
      CHECK(!irm_rec_active() && !g_st.rec && !strcmp(ri.end_reason, "Karte voll"), "beendet, Grund: '%s'", ri.end_reason);
      g_free = 1ull << 40; }
    wipe();
    printf(fails ? "\n%d FEHLER\n" : "\nalle Tests bestanden\n", fails);
    return fails ? 1 : 0;
}
