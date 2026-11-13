#include "irm_dir_query.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irm_kiwi_uri.h"

/* Begrenztes Kopieren ohne strncpy; Quellgroesse wird mitgegeben (sonst meldet GCC -Wstringop-overread, wenn Ziel groesser als Quelle ist) */
static void scopy(char *d, size_t dn, const char *s, size_t sn)
{
    size_t max = dn - 1 < sn ? dn - 1 : sn;
    size_t l = strnlen(s, max);
    memcpy(d, s, l);
    d[l] = 0;
}
#define SCOPY(dst, src) scopy((dst), sizeof(dst), (src), sizeof(src))

static void make_query(kiwi_dir_query_t *q, const irm_dir_filter_t *f)
{
    kiwi_dir_query_init(q);                 /* schliesst offline/https/privat aus */
    q->country = f->country;
    q->min_snr = f->min_snr;
    q->only_free = f->only_free ? 1 : 0;
    q->freq_hz = f->freq_hz;
    SCOPY(q->text, f->text);
}

typedef struct { uint32_t idx; uint8_t snr, users; } key_t_;

static int key_cmp(const void *a, const void *b)
{
    const key_t_ *x = a, *y = b;
    if (x->snr != y->snr) return x->snr > y->snr ? -1 : 1;      /* SNR absteigend */
    if (x->users != y->users) return x->users < y->users ? -1 : 1;   /* weniger Nutzer zuerst */
    return x->idx < y->idx ? -1 : (x->idx > y->idx);           /* stabil */
}

int irm_dir_search(const kiwi_dir_db_t *db, const irm_dir_filter_t *f, uint32_t offset, uint32_t limit, irm_dir_hit_t *out, uint32_t *total)
{
    if (!db || !f || (limit && !out)) return KIWI_DIR_ERR_ARG;
    if (!kiwi_dir_db_valid(db)) { if (total) *total = 0; return 0; }
    kiwi_dir_query_t q;
    make_query(&q, f);
    uint32_t cur = 0, idx, n = 0, seen = 0;
    kiwi_dir_rec_t r;

    key_t_ *keys = NULL;
    if (f->sort == IRM_DIR_SORT_SNR) keys = malloc(sizeof(key_t_) * (kiwi_dir_db_count(db) ? kiwi_dir_db_count(db) : 1));   /* Fehlschlag: Listenreihenfolge */

    int rc;
    while ((rc = kiwi_dir_db_find(db, &q, &cur, &r, &idx)) == 1) {
        if (keys) { keys[seen].idx = idx; keys[seen].snr = kiwi_dir_rec_snr(&r); keys[seen].users = r.users; }
        else if (seen >= offset && n < limit) { out[n].idx = idx; out[n].rec = r; n++; }
        seen++;
    }
    if (rc < 0) { free(keys); return rc; }
    if (total) *total = seen;
    if (keys) {
        qsort(keys, seen, sizeof(key_t_), key_cmp);
        for (uint32_t i = offset; i < seen && n < limit; i++) {
            if (kiwi_dir_db_get(db, keys[i].idx, &out[n].rec) != KIWI_DIR_OK) { free(keys); return KIWI_DIR_ERR_STORE; }
            out[n].idx = keys[i].idx;
            n++;
        }
        free(keys);
    }
    return (int)n;
}

static int country_cmp(const void *a, const void *b)
{
    const irm_dir_country_t *x = a, *y = b;
    return strcasecmp(x->name, y->name);
}

int irm_dir_countries(const kiwi_dir_db_t *db, irm_dir_country_t *out, size_t max)
{
    if (!db || (max && !out)) return KIWI_DIR_ERR_ARG;
    if (!kiwi_dir_db_valid(db)) return 0;
    uint16_t counts[256];
    int rc = kiwi_dir_db_count_by_country(db, counts);
    if (rc < 0) return rc;
    size_t n = 0;
    for (unsigned id = 1; id <= kiwi_dir_db_country_count(db) && id < 256 && n < max; id++) {
        if (!counts[id]) continue;
        out[n].id = (uint8_t)id;
        out[n].count = counts[id];
        if (kiwi_dir_db_country_name(db, id, out[n].name) != KIWI_DIR_OK) continue;
        n++;
    }
    qsort(out, n, sizeof(out[0]), country_cmp);
    return (int)n;
}

bool irm_dir_build_uri(const kiwi_dir_rec_t *rec, uint32_t freq_hz, const char *mode, char *out, size_t n, uint32_t *used_freq)
{
    if (!rec || !out || !n || !rec->host[0] || !rec->port) return false;
    irm_kiwi_uri_t u;
    memset(&u, 0, sizeof u);
    SCOPY(u.host, rec->host);
    u.port = rec->port;
    int mi = irm_kiwi_mode_index(mode);
    strcpy(u.mode, irm_kiwi_mode_name(mi < 0 ? 0 : (size_t)mi));
    if (rec->band_hi_hz > rec->band_lo_hz) {                          /* Bereich bekannt */
        if (freq_hz < rec->band_lo_hz) freq_hz = rec->band_lo_hz;
        if (freq_hz > rec->band_hi_hz) freq_hz = rec->band_hi_hz;
    }
    if (freq_hz < IRM_KIWI_FREQ_MIN_HZ) freq_hz = IRM_KIWI_FREQ_MIN_HZ;
    if (freq_hz > IRM_KIWI_FREQ_MAX_HZ) freq_hz = IRM_KIWI_FREQ_MAX_HZ;
    u.freq_hz = freq_hz;
    if (used_freq) *used_freq = freq_hz;
    return irm_kiwi_uri_build(&u, out, n);
}

void irm_dir_display_name(const kiwi_dir_rec_t *rec, char *out, size_t n)
{
    if (!out || !n) return;
    const char *nm = rec->name[0] ? rec->name : rec->host;
    int len = rec->place[0] ? snprintf(out, n, "%s (%s)", nm, rec->place) : snprintf(out, n, "%s", nm);
    if (len < 0) { out[0] = 0; return; }
    if ((size_t)len >= n) {                        /* abgeschnitten: out[0..n-2] sind Inhalt, out[n-1] ist die Endnull */
        size_t k = n - 1;                          /* Position hinter dem letzten Inhaltsbyte */
        size_t st = k;
        while (st > 0 && ((unsigned char)out[st - 1] & 0xC0) == 0x80) st--;   /* Fortsetzungsbytes ueberspringen */
        if (st > 0) {
            unsigned char lead = (unsigned char)out[st - 1];
            size_t need = lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : (lead >= 0xC0 ? 2 : 1));
            if (need > 1 && (k - (st - 1)) < need) k = st - 1;               /* Zeichen unvollstaendig -> weglassen */
        }
        out[k] = 0;
    }
}

const char *irm_dir_err_text(int err)
{
    if (err == 0) return "";
    if (err == IRM_DIR_ERR_NOMEM) return "zu wenig Speicher";
    return kiwi_dir_strerror(err);
}
