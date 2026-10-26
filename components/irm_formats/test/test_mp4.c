/* Host-Test: gcc -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_mp4.c test/test_mp4.c -o /tmp/tm && /tmp/tm */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irm_mp4.h"

#define FC(a,b,c,d) ((((uint32_t)(a)) << 24) | (((uint32_t)(b)) << 16) | (((uint32_t)(c)) << 8) | (uint32_t)(d))
static void w32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void w64(uint8_t *p, uint64_t v) { w32(p, (uint32_t)(v >> 32)); w32(p + 4, (uint32_t)v); }
static uint32_t r32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint64_t r64(const uint8_t *p) { return ((uint64_t)r32(p) << 32) | r32(p + 4); }
static uint8_t pat(size_t i) { return (uint8_t)(i * 7 + 3); }

/* Atom (Groesse+Typ+Nutzlast) an b anhaengen, neue Laenge zurueck */
static size_t atom(uint8_t *b, size_t at, uint32_t type, const uint8_t *pl, size_t pl_len)
{
    w32(b + at, (uint32_t)(8 + pl_len)); w32(b + at + 4, type);
    if (pl_len) memcpy(b + at + 8, pl, pl_len);
    return at + 8 + pl_len;
}

static size_t build_moov(uint8_t *out, bool co64, const uint64_t *offs, int n)
{
    uint8_t co[256] = { 0 }; size_t cl = 8;                      /* version/flags + count */
    w32(co + 4, (uint32_t)n);
    for (int i = 0; i < n; i++) { if (co64) { w64(co + cl, offs[i]); cl += 8; } else { w32(co + cl, (uint32_t)offs[i]); cl += 4; } }
    uint8_t stbl[512]; size_t s = 0; uint8_t z[64] = { 0 };
    s = atom(stbl, s, FC('s','t','s','d'), z, 16);
    s = atom(stbl, s, co64 ? FC('c','o','6','4') : FC('s','t','c','o'), co, cl);
    uint8_t minf[600]; size_t m = 0;
    m = atom(minf, m, FC('s','m','h','d'), z, 8);
    m = atom(minf, m, FC('s','t','b','l'), stbl, s);
    uint8_t mdia[700]; size_t d = 0;
    d = atom(mdia, d, FC('m','d','h','d'), z, 24);
    d = atom(mdia, d, FC('m','i','n','f'), minf, m);
    uint8_t trak[800]; size_t t = 0;
    t = atom(trak, t, FC('t','k','h','d'), z, 32);
    t = atom(trak, t, FC('m','d','i','a'), mdia, d);
    uint8_t moov[900]; size_t v = 0;
    v = atom(moov, v, FC('m','v','h','d'), z, 64);
    v = atom(moov, v, FC('t','r','a','k'), trak, t);
    return atom(out, 0, FC('m','o','o','v'), moov, v);
}

/* Datei: ftyp, free, mdat(2000 Byte), moov  (moov_first: ftyp, moov, mdat) */
static size_t write_file(const char *path, bool co64, bool big_mdat, bool moov_first, uint64_t *first_chunk)
{
    uint8_t ftyp[12] = { 'M','4','A',' ', 0,0,0,0, 'M','4','A',' ' }, fr[8] = { 0 };
    uint8_t file[8192]; size_t n = 0;
    n = atom(file, n, FC('f','t','y','p'), ftyp, sizeof ftyp);
    n = atom(file, n, FC('f','r','e','e'), fr, sizeof fr);
    uint8_t moov[1024]; uint64_t offs[3];
    size_t moov_len = 0;
    /* moov-Laenge haengt nicht von den Offsetwerten ab -> zweimal bauen */
    moov_len = build_moov(moov, co64, (uint64_t[]){0, 0, 0}, 3);
    size_t mdat_hdr = big_mdat ? 16 : 8;
    size_t data_start = n + (moov_first ? moov_len : 0) + mdat_hdr;
    offs[0] = data_start; offs[1] = data_start + 500; offs[2] = data_start + 1200;
    *first_chunk = offs[0];
    build_moov(moov, co64, offs, 3);
    if (moov_first) { memcpy(file + n, moov, moov_len); n += moov_len; }
    if (big_mdat) { w32(file + n, 1); w32(file + n + 4, FC('m','d','a','t')); w64(file + n + 8, 16 + 2000); }
    else { w32(file + n, 8 + 2000); w32(file + n + 4, FC('m','d','a','t')); }
    n += mdat_hdr;
    for (int i = 0; i < 2000; i++) file[n + i] = pat(i);
    n += 2000;
    if (!moov_first) { memcpy(file + n, moov, moov_len); n += moov_len; }
    FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(file, 1, n, f) == n); fclose(f);
    return n;
}

static void check_variant(bool co64, bool big_mdat)
{
    const char *p = "/tmp/irm_t_a.m4a";
    uint64_t first;
    size_t fsz = write_file(p, co64, big_mdat, false, &first);
    bool needs = false;
    assert(irm_mp4_probe(p, &needs) == 0 && needs);

    irm_mp4_fs_t *fs;
    assert(irm_mp4_fs_open(p, &fs) == 0);
    assert(irm_mp4_fs_size(fs) == fsz);
    uint8_t *v = malloc(fsz + 16);
    size_t got = 0;
    for (;;) { size_t r = irm_mp4_fs_read(fs, v + got, 97); if (!r) break; got += r; }   /* krumme Blockgroesse */
    assert(got == fsz);
    /* wahlfreier Zugriff stimmt mit sequentiellem Lesen ueberein */
    for (size_t s = 0; s + 200 <= fsz; s += 331) {
        uint8_t tmp[200];
        assert(irm_mp4_fs_seek(fs, s) == 0 && irm_mp4_fs_read(fs, tmp, 200) == 200 && memcmp(tmp, v + s, 200) == 0);
    }
    assert(irm_mp4_fs_seek(fs, fsz + 1) != 0);
    irm_mp4_fs_close(fs);

    /* virtuelle Datei: jetzt moov vor mdat */
    FILE *g = fopen("/tmp/irm_t_b.m4a", "wb"); fwrite(v, 1, fsz, g); fclose(g);
    g = fopen("/tmp/irm_t_b.m4a", "rb");
    irm_mp4_info_t in; assert(irm_mp4_scan(g, &in) == 0); fclose(g);
    assert(!irm_mp4_needs_faststart(&in) && in.moov_start == in.ftyp_end && in.file_size == fsz);
    size_t moov_size = in.moov_end - in.moov_start;

    /* stco/co64-Eintraege im virtuellen moov suchen und pruefen: Verweise treffen die richtigen Daten */
    const uint8_t tag[4] = { co64 ? 'c' : 's', co64 ? 'o' : 't', co64 ? '6' : 'c', co64 ? '4' : 'o' };
    size_t t = 0; for (; t + 12 < fsz; t++) if (memcmp(v + t, tag, 4) == 0) break;
    assert(t + 12 < fsz && r32(v + t + 8) == 3);
    const size_t rel[3] = { 0, 500, 1200 };
    for (int i = 0; i < 3; i++) {
        uint64_t off = co64 ? r64(v + t + 12 + 8 * i) : r32(v + t + 12 + 4 * i);
        assert(off == first + rel[i] + moov_size);                          /* um moov-Laenge verschoben */
        for (int k = 0; k < 50; k++) assert(v[off + k] == pat(rel[i] + k));  /* ... und zeigt auf dieselben Mediendaten */
    }
    free(v);
}

int main(void)
{
    check_variant(false, false);
    check_variant(true, false);
    check_variant(false, true);     /* mdat mit 64-Bit-Groesse */
    check_variant(true, true);

    uint64_t first;
    write_file("/tmp/irm_t_c.m4a", false, false, true, &first);   /* moov bereits vorne */
    bool needs = true;
    assert(irm_mp4_probe("/tmp/irm_t_c.m4a", &needs) == 0 && !needs);
    irm_mp4_fs_t *fs = NULL;
    assert(irm_mp4_fs_open("/tmp/irm_t_c.m4a", &fs) != 0 && fs == NULL);

    FILE *g = fopen("/tmp/irm_t_d.m4a", "wb"); fputs("kein mp4 sondern nur text, nichts weiter", g); fclose(g);
    assert(irm_mp4_probe("/tmp/irm_t_d.m4a", &needs) != 0);
    assert(irm_mp4_probe("/tmp/gibt_es_nicht.m4a", &needs) != 0);
    assert(irm_mp4_has_mp4_extension("/sdcard/a.M4A") && irm_mp4_has_mp4_extension("x.mp4") && !irm_mp4_has_mp4_extension("x.mp3"));
    puts("irm_mp4: alle Tests ok");
    return 0;
}
