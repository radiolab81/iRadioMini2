#include "irm_mp4.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define T_FTYP FOURCC('f','t','y','p')
#define T_MDAT FOURCC('m','d','a','t')
#define T_MOOV FOURCC('m','o','o','v')
#define T_TRAK FOURCC('t','r','a','k')
#define T_MDIA FOURCC('m','d','i','a')
#define T_MINF FOURCC('m','i','n','f')
#define T_STBL FOURCC('s','t','b','l')
#define T_STCO FOURCC('s','t','c','o')
#define T_CO64 FOURCC('c','o','6','4')

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }

static bool read_header(FILE *f, uint64_t pos, uint64_t fsize, uint64_t *size, uint32_t *type)
{
    uint8_t h[16];
    if (pos + 8 > fsize || fseek(f, (long)pos, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8) return false;
    uint64_t sz = be32(h);
    uint32_t hdr = 8;
    *type = be32(h + 4);
    if (sz == 1) {
        if (pos + 16 > fsize || fread(h + 8, 1, 8, f) != 8) return false;
        sz = be64(h + 8);
        hdr = 16;
    } else if (sz == 0) {
        sz = fsize - pos;
    }
    if (sz < hdr || pos + sz > fsize) return false;
    *size = sz;
    return true;
}

int irm_mp4_scan(FILE *f, irm_mp4_info_t *info)
{
    memset(info, 0, sizeof(*info));
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long end = ftell(f);
    if (end < 8) return -1;
    info->file_size = (uint64_t)end;
    uint64_t pos = 0;
    for (int n = 0; n < 4096 && pos < info->file_size; n++) {
        uint64_t sz; uint32_t type;
        if (!read_header(f, pos, info->file_size, &sz, &type)) break;
        if (type == T_FTYP && pos == 0) info->ftyp_end = sz;
        else if (type == T_MDAT && !info->has_mdat) { info->has_mdat = true; info->mdat_start = pos; }
        else if (type == T_MOOV && !info->has_moov) { info->has_moov = true; info->moov_start = pos; info->moov_end = pos + sz; }
        pos += sz;
    }
    return (info->has_moov && info->has_mdat) ? 0 : -2;
}

bool irm_mp4_needs_faststart(const irm_mp4_info_t *i) { return i->has_moov && i->has_mdat && i->mdat_start < i->moov_start; }

bool irm_mp4_has_mp4_extension(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && (strcasecmp(dot, ".m4a") == 0 || strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".m4b") == 0);
}

/* Chunk-Offsets in stco/co64 anpassen: Offsets im Bereich [lo,hi) werden um delta erhoeht (dort liegt die verschobene Mediendaten-Region). */
static int patch_walk(uint8_t *b, size_t start, size_t end, uint64_t lo, uint64_t hi, uint64_t delta, int depth)
{
    if (depth > 8) return -1;
    size_t pos = start;
    while (pos + 8 <= end) {
        uint64_t sz = be32(b + pos);
        uint32_t type = be32(b + pos + 4), hdr = 8;
        if (sz == 1) { if (pos + 16 > end) return -1; sz = be64(b + pos + 8); hdr = 16; }
        else if (sz == 0) sz = end - pos;
        if (sz < hdr || pos + sz > end) return -1;
        size_t p = pos + hdr, e = pos + (size_t)sz;
        if (type == T_MOOV || type == T_TRAK || type == T_MDIA || type == T_MINF || type == T_STBL) {
            int r = patch_walk(b, p, e, lo, hi, delta, depth + 1);
            if (r < 0) return r;
        } else if (type == T_STCO || type == T_CO64) {
            if (p + 8 > e) return -1;
            uint32_t count = be32(b + p + 4);
            size_t esz = (type == T_STCO) ? 4 : 8;
            if ((uint64_t)count * esz > (uint64_t)(e - p - 8)) return -1;
            uint8_t *q = b + p + 8;
            for (uint32_t i = 0; i < count; i++, q += esz) {
                uint64_t v = (esz == 4) ? be32(q) : be64(q);
                if (v >= lo && v < hi) {
                    v += delta;
                    if (esz == 4) { if (v > 0xFFFFFFFFull) return -2; put32(q, (uint32_t)v); }
                    else put64(q, v);
                }
            }
        }
        pos += (size_t)sz;
    }
    return 0;
}

struct irm_mp4_fs {
    FILE *f;
    irm_mp4_info_t info;
    uint8_t *moov;
    size_t moov_len;
    uint64_t pos;
    uint64_t fpos;      /* aktuelle Position des FILE-Handles */
};

int irm_mp4_probe(const char *path, bool *needs)
{
    *needs = false;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    irm_mp4_info_t info;
    int r = irm_mp4_scan(f, &info);
    fclose(f);
    if (r == 0) *needs = irm_mp4_needs_faststart(&info);
    return r;
}

int irm_mp4_fs_open(const char *path, irm_mp4_fs_t **out)
{
    *out = NULL;
    irm_mp4_fs_t *fs = calloc(1, sizeof(*fs));
    if (!fs) return -3;
    fs->f = fopen(path, "rb");
    if (!fs->f) { free(fs); return -1; }
    int r = irm_mp4_scan(fs->f, &fs->info);
    if (r != 0 || !irm_mp4_needs_faststart(&fs->info)) { fclose(fs->f); free(fs); return r != 0 ? r : -4; }
    uint64_t m = fs->info.moov_end - fs->info.moov_start;
    if (m > IRM_MP4_MAX_MOOV) { fclose(fs->f); free(fs); return -5; }
    fs->moov_len = (size_t)m;
    fs->moov = malloc(fs->moov_len);
    if (!fs->moov) { fclose(fs->f); free(fs); return -3; }
    if (fseek(fs->f, (long)fs->info.moov_start, SEEK_SET) != 0 || fread(fs->moov, 1, fs->moov_len, fs->f) != fs->moov_len) {
        free(fs->moov); fclose(fs->f); free(fs); return -1;
    }
    /* moov wandert vor die Mediendaten: Daten in [ftyp_end, moov_start) verschieben sich um die moov-Laenge */
    r = patch_walk(fs->moov, 0, fs->moov_len, fs->info.ftyp_end, fs->info.moov_start, fs->moov_len, 0);
    if (r != 0) { free(fs->moov); fclose(fs->f); free(fs); return -6; }
    fs->fpos = (uint64_t)-1;
    *out = fs;
    return 0;
}

uint64_t irm_mp4_fs_size(const irm_mp4_fs_t *fs) { return fs->info.file_size; }

int irm_mp4_fs_seek(irm_mp4_fs_t *fs, uint64_t pos)
{
    if (pos > fs->info.file_size) return -1;
    fs->pos = pos;
    return 0;
}

static size_t file_read(irm_mp4_fs_t *fs, uint64_t off, uint8_t *dst, size_t n)
{
    if (fs->fpos != off && fseek(fs->f, (long)off, SEEK_SET) != 0) { fs->fpos = (uint64_t)-1; return 0; }
    size_t r = fread(dst, 1, n, fs->f);
    fs->fpos = off + r;
    return r;
}

size_t irm_mp4_fs_read(irm_mp4_fs_t *fs, void *buf, size_t n)
{
    uint8_t *dst = buf;
    size_t done = 0;
    const uint64_t a = fs->info.ftyp_end, m = fs->moov_len, ms = fs->info.moov_start, me = fs->info.moov_end, total = fs->info.file_size;
    while (done < n && fs->pos < total) {
        uint64_t p = fs->pos, avail, src;
        size_t got;
        if (p < a)               { src = p;                  avail = a - p;            got = 0; }          /* ftyp (Datei) */
        else if (p < a + m)      { src = p - a;              avail = a + m - p;        got = 1; }          /* moov (RAM) */
        else if (p < ms + m)     { src = a + (p - a - m);    avail = ms + m - p;       got = 0; }          /* Mediendaten bis altes moov */
        else                     { src = me + (p - ms - m);  avail = total - p;        got = 0; }          /* Reste nach altem moov */
        size_t chunk = (n - done < avail) ? (n - done) : (size_t)avail;
        size_t r;
        if (got == 1) { memcpy(dst + done, fs->moov + src, chunk); r = chunk; }
        else r = file_read(fs, src, dst + done, chunk);
        if (r == 0) break;
        done += r;
        fs->pos += r;
    }
    return done;
}

void irm_mp4_fs_close(irm_mp4_fs_t *fs)
{
    if (!fs) return;
    if (fs->f) fclose(fs->f);
    free(fs->moov);
    free(fs);
}
