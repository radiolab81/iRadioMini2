/* Host-Test: Suche/Paging/Sortierung/Laender/URI-Bau auf einer echten Datenbank (aus test/kiwisdr_sample.js, ueber kiwi_dir_update gefuellt).
 * Aufruf siehe README, Abschnitt Tests. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irm_dir_query.h"
#include "irm_kiwi_uri.h"
#include "kiwi_dir_update.h"

static int fails;
#define CHECK(c, ...) do { int ok_ = !!(c); printf("  %s ", ok_ ? "ok  " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

#define SLOT (192u * 1024u)
static uint8_t g_mem[KIWI_DIR_SLOTS][SLOT];
static int m_read(void *c, int s, size_t o, void *b, size_t n) { (void)c; if (o + n > SLOT) return -1; memcpy(b, g_mem[s] + o, n); return 0; }
static int m_erase(void *c, int s) { (void)c; memset(g_mem[s], 0xFF, SLOT); return 0; }
static int m_write(void *c, int s, size_t o, const void *b, size_t n) { (void)c; if (o + n > SLOT) return -1; memcpy(g_mem[s] + o, b, n); return 0; }
static const kiwi_dir_store_t g_store = { SLOT, 4096, m_read, m_erase, m_write, NULL };

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "test/kiwisdr_sample.js";
    FILE *f = fopen(path, "rb"); assert(f);
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *txt = malloc((size_t)sz); assert(fread(txt, 1, (size_t)sz, f) == (size_t)sz); fclose(f);

    static kiwi_dir_db_t db;
    m_erase(NULL, 0); m_erase(NULL, 1);
    CHECK(kiwi_dir_db_open(&db, &g_store, 0) == KIWI_DIR_ERR_EMPTY, "leere Datenbank");
    irm_dir_filter_t F = { .sort = IRM_DIR_SORT_LIST };
    uint32_t total = 99;
    CHECK(irm_dir_search(&db, &F, 0, 10, (irm_dir_hit_t[10]){ 0 }, &total) == 0 && total == 0, "Suche in leerer DB: 0 Treffer");
    irm_dir_country_t cc[4];
    CHECK(irm_dir_countries(&db, cc, 4) == 0, "Laenderliste in leerer DB leer");

    static kiwi_dir_update_t up;
    kiwi_dir_result_t res;
    assert(kiwi_dir_update_begin(&up, &g_store, &db) == KIWI_DIR_OK);
    for (long o = 0; o < sz; o += 777) assert(kiwi_dir_update_push(&up, txt + o, (size_t)(sz - o < 777 ? sz - o : 777)) == KIWI_DIR_OK);   /* krumm gestueckelt */
    assert(kiwi_dir_update_end(&up, &res) == KIWI_DIR_OK);
    printf("== Datenbank: %u Server, %u Laender\n", (unsigned)res.entries, (unsigned)res.countries);
    CHECK(res.entries >= 30, "echte Testdaten geladen (%u)", (unsigned)res.entries);

    printf("== Suche: Listenreihenfolge, Paging\n");
    uint32_t all = 0;
    CHECK(irm_dir_search(&db, &F, 0, 0, NULL, &all) == 0 && all >= 30, "total = %u verbindbare Server (limit 0 zaehlt nur)", (unsigned)all);
    irm_dir_hit_t *h = malloc(sizeof(*h) * 1000), *p2 = malloc(sizeof(*p2) * 7);
    int n = irm_dir_search(&db, &F, 0, 1000, h, &total);
    CHECK(n == (int)all && total == all, "alles in einem Aufruf (%d)", n);
    uint32_t prev = 0; int mono = 1, usable = 1;
    for (int i = 0; i < n; i++) { if (i && h[i].idx <= prev) mono = 0; prev = h[i].idx; if (h[i].rec.flags & KIWI_DIR_F_UNUSABLE) usable = 0; }
    CHECK(mono && usable, "aufsteigende Indizes, keine offline/https/privat-Server");
    int ok = 1; uint32_t seen = 0;
    for (uint32_t off = 0; off < all + 14; off += 7) {
        int k = irm_dir_search(&db, &F, off, 7, p2, &total);
        if (total != all) ok = 0;
        for (int i = 0; i < k; i++) if (off + (uint32_t)i >= all || p2[i].idx != h[off + (uint32_t)i].idx) ok = 0;
        if (off >= all && k != 0) ok = 0;
        seen += (uint32_t)k;
    }
    CHECK(ok && seen == all, "Seiten zu 7 ergeben exakt die Gesamtliste (%u), hinter dem Ende 0 Treffer", (unsigned)seen);

    printf("== Sortierung nach SNR\n");
    irm_dir_filter_t S = F; S.sort = IRM_DIR_SORT_SNR;
    irm_dir_hit_t *hs = malloc(sizeof(*hs) * 1000);
    int ns = irm_dir_search(&db, &S, 0, 1000, hs, &total);
    int desc = 1;
    for (int i = 1; i < ns; i++) {
        uint8_t a = kiwi_dir_rec_snr(&hs[i - 1].rec), b = kiwi_dir_rec_snr(&hs[i].rec);
        if (a < b || (a == b && hs[i - 1].rec.users > hs[i].rec.users)) desc = 0;
    }
    CHECK(ns == n && total == all && desc, "SNR absteigend, bei Gleichstand weniger Nutzer zuerst (%d Treffer, bester SNR %u dB)", ns, ns ? kiwi_dir_rec_snr(&hs[0].rec) : 0);
    uint64_t sum1 = 0, sum2 = 0; for (int i = 0; i < n; i++) { sum1 += h[i].idx; sum2 += hs[i].idx; }
    CHECK(sum1 == sum2, "gleiche Treffermenge wie in Listenreihenfolge");
    kiwi_dir_rec_t best; uint32_t bi;
    CHECK(kiwi_dir_db_best(&db, &(kiwi_dir_query_t){ 0 }, &best, &bi) >= 0, "Referenz kiwi_dir_db_best vorhanden");
    ok = 1; seen = 0;
    for (uint32_t off = 0; off < all; off += 7) { int k = irm_dir_search(&db, &S, off, 7, p2, NULL); for (int i = 0; i < k; i++) if (p2[i].idx != hs[off + (uint32_t)i].idx) ok = 0; seen += (uint32_t)k; }
    CHECK(ok && seen == all, "Paging in SNR-Reihenfolge konsistent");

    printf("== Filter\n");
    irm_dir_country_t *co = malloc(sizeof(*co) * 256);
    int nc = irm_dir_countries(&db, co, 256);
    uint32_t sumc = 0; int sorted = 1;
    for (int i = 0; i < nc; i++) { sumc += co[i].count; if (i && strcasecmp(co[i - 1].name, co[i].name) > 0) sorted = 0; }
    CHECK(nc > 3 && sorted, "%d Laender, alphabetisch", nc);
    ok = 1; uint32_t sumsearch = 0;
    for (int i = 0; i < nc; i++) {
        irm_dir_filter_t C = F; C.country = co[i].id;
        int k = irm_dir_search(&db, &C, 0, 1000, h, &total);
        if ((uint32_t)k != co[i].count || total != co[i].count) ok = 0;
        for (int j = 0; j < k; j++) if (h[j].rec.country != co[i].id) ok = 0;
        sumsearch += total;
    }
    CHECK(ok, "Land-Filter: Trefferzahl == Zahl der Laenderliste, alle Treffer im Land");
    CHECK(sumsearch == sumc && sumsearch <= all, "Summe ueber Laender (%u) == Summe der Laenderliste, <= alle (%u); Rest ohne Land", (unsigned)sumsearch, (unsigned)all);
    { irm_dir_filter_t C = F; strcpy(C.text, "zzzzqqq"); CHECK(irm_dir_search(&db, &C, 0, 10, h, &total) == 0 && total == 0, "Text ohne Treffer"); }
    { irm_dir_filter_t C = F; n = irm_dir_search(&db, &F, 0, 1, h, &total); char t[24]; strncpy(t, h[0].rec.host, 10); t[10] = 0;
      for (char *c = t; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;               /* Gross/Klein egal */
      snprintf(C.text, sizeof C.text, "%s", t);
      int k = irm_dir_search(&db, &C, 0, 1000, h, &total); int found = 0; for (int i = 0; i < k; i++) found += 1;
      CHECK(k >= 1 && found == k, "Text-Filter (%s) findet mind. den Ursprungsserver (%d)", t, k); }
    { irm_dir_filter_t C = F; C.only_free = true;
      int k = irm_dir_search(&db, &C, 0, 1000, h, &total); int bad = 0; for (int i = 0; i < k; i++) if (h[i].rec.users_max && h[i].rec.users >= h[i].rec.users_max) bad++;
      CHECK(bad == 0 && total <= all, "nur freie Kanaele: %u von %u", (unsigned)total, (unsigned)all); }
    { irm_dir_filter_t C = F; C.min_snr = 30;
      int k = irm_dir_search(&db, &C, 0, 1000, h, &total); int bad = 0; for (int i = 0; i < k; i++) if (kiwi_dir_rec_snr(&h[i].rec) < 30) bad++;
      CHECK(bad == 0 && total < all, "min. SNR 30 dB: %u Treffer", (unsigned)total); }
    { irm_dir_filter_t C = F; C.freq_hz = 144500000u;      /* 144,5 MHz: kein HF-Kiwi deckt das ab */
      CHECK(irm_dir_search(&db, &C, 0, 10, h, &total) >= 0 && total < all, "Frequenzfilter 144,5 MHz: %u Treffer (weniger als alle)", (unsigned)total);
      C.freq_hz = 7055000u; int k = irm_dir_search(&db, &C, 0, 1000, h, &total); int bad = 0;
      for (int i = 0; i < k; i++) if (h[i].rec.band_hi_hz && (7055000u < h[i].rec.band_lo_hz || 7055000u > h[i].rec.band_hi_hz)) bad++;
      CHECK(k > 0 && bad == 0, "Frequenzfilter 7055 kHz: %d Treffer, alle decken die Frequenz ab", k); }
    CHECK(irm_dir_search(NULL, &F, 0, 1, h, &total) < 0 && irm_dir_search(&db, NULL, 0, 1, h, &total) < 0 && irm_dir_search(&db, &F, 0, 5, NULL, &total) < 0, "ungueltige Argumente abgelehnt");

    printf("== URI-Bau\n");
    irm_dir_search(&db, &F, 0, 1, h, &total);
    kiwi_dir_rec_t r = h[0].rec; char uri[160]; uint32_t used;
    r.band_lo_hz = 1800000; r.band_hi_hz = 30000000;
    CHECK(irm_dir_build_uri(&r, 7055500, "usb", uri, sizeof uri, &used) && used == 7055500, "Frequenz im Bereich unveraendert");
    irm_kiwi_uri_t pu;
    CHECK(irm_kiwi_uri_parse(uri, &pu) && pu.freq_hz == 7055500 && !strcmp(pu.mode, "usb") && pu.port == r.port && !strcmp(pu.host, r.host), "URI parsebar und identisch: %s", uri);
    CHECK(irm_dir_build_uri(&r, 77500, "am", uri, sizeof uri, &used) && used == 1800000, "Frequenz unter dem Bereich -> unterer Rand (%u)", (unsigned)used);
    CHECK(irm_dir_build_uri(&r, 31000000, NULL, uri, sizeof uri, &used) && used == 30000000 && strstr(uri, "mode=am"), "darueber -> oberer Rand, Modus NULL -> am");
    r.band_lo_hz = r.band_hi_hz = 0;
    CHECK(irm_dir_build_uri(&r, 77500, "bogus", uri, sizeof uri, &used) && used == 77500 && strstr(uri, "mode=am"), "Bereich unbekannt: keine Begrenzung, ungueltiger Modus -> am");
    CHECK(!irm_dir_build_uri(&r, 77500, "am", uri, 10, &used) && !irm_dir_build_uri(NULL, 1, "am", uri, sizeof uri, NULL), "zu kleiner Puffer / NULL abgelehnt");

    printf("== Anzeigename\n");
    kiwi_dir_rec_t u8; memset(&u8, 0, sizeof u8);
    strcpy(u8.host, "h.example"); strcpy(u8.name, "Gro\xc3\x9f" "empf\xc3\xa4nger"); strcpy(u8.place, "M\xc3\xbcnchen");
    char nm[64];
    irm_dir_display_name(&u8, nm, sizeof nm); CHECK(!strcmp(nm, "Gro\xc3\x9f" "empf\xc3\xa4nger (M\xc3\xbcnchen)"), "Name (Ort)");
    u8.place[0] = 0; irm_dir_display_name(&u8, nm, sizeof nm); CHECK(!strcmp(nm, "Gro\xc3\x9f" "empf\xc3\xa4nger"), "ohne Ort");
    u8.name[0] = 0; irm_dir_display_name(&u8, nm, sizeof nm); CHECK(!strcmp(nm, "h.example"), "ohne Name: Host");
    strcpy(u8.name, "Gro\xc3\x9f" "empf\xc3\xa4nger"); strcpy(u8.place, "M\xc3\xbcnchen \xe2\x82\xac" "uro \xf0\x9f\x93\xa1" "x");
    int badcut = 0;
    for (size_t k = 1; k < 48; k++) {
        char b[48]; memset(b, 'Z', sizeof b); irm_dir_display_name(&u8, b, k);
        size_t l = strnlen(b, k); if (l >= k) { badcut++; continue; }               /* terminiert? */
        for (size_t i = 0; i < l; ) {                                                  /* gueltiges UTF-8? */
            unsigned char c = (unsigned char)b[i]; size_t need = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
            if (!need || i + need > l) { badcut++; break; }
            for (size_t j = 1; j < need; j++) if (((unsigned char)b[i + j] & 0xC0) != 0x80) { badcut++; break; }
            i += need;
        }
    }
    CHECK(badcut == 0, "jede Puffergroesse 1..47 liefert terminiertes, gueltiges UTF-8");

    printf("== Ergebnis: %s (%d Fehler)\n", fails ? "FEHLER" : "OK", fails);
    return fails ? 1 : 0;
}
