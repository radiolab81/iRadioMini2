#include "irm_web_dir.h"
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- UTF-8 */

size_t irm_web_utf8_clean(char *s)
{
    unsigned char *p = (unsigned char *)s;
    size_t i = 0;
    while (p[i]) {
        unsigned char c = p[i];
        size_t need = 0;
        unsigned char lo = 0x80, hi = 0xBF;          /* erlaubter Bereich des 1. Folgebytes */
        if (c < 0x20 || c == 0x7F) { p[i++] = ' '; continue; }
        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        bool ok = need > 0;
        for (size_t k = 1; ok && k <= need; k++) {
            unsigned char d = p[i + k];
            if (d == 0) { ok = false; break; }
            if (k == 1 ? (d < lo || d > hi) : ((d & 0xC0) != 0x80)) ok = false;
        }
        if (ok) i += need + 1; else p[i++] = '?';
    }
    return i;
}

/* ---------------------------------------------------------------- Query */

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Wert [v, v+n) dekodieren; false bei zu langem Ergebnis oder kaputtem %XX */
static bool decode(const char *v, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = v[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= n) return false;                 /* %XX muss vollstaendig im Wert liegen */
            int h = hexv(v[i + 1]), l = hexv(v[i + 2]);
            if (h < 0 || l < 0) return false;
            c = (char)(h * 16 + l);
            i += 2;
        }
        if (c == 0 || o + 1 >= cap) return false;
        out[o++] = c;
    }
    out[o] = 0;
    return true;
}

static bool to_u32(const char *s, uint32_t max, uint32_t *out)
{
    if (!*s) return false;
    uint64_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > max) return false;
    }
    *out = (uint32_t)v;
    return true;
}

bool irm_web_dir_parse_query(const char *qs, irm_web_dir_req_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    out->limit = IRM_WEB_DIR_PAGE_DEFAULT;
    if (!qs) return true;
    while (*qs) {
        const char *e = qs;
        while (*e && *e != '&') e++;
        const char *eq = memchr(qs, '=', (size_t)(e - qs));
        if (eq) {
            const size_t kl = (size_t)(eq - qs);
            char key[12], val[64];
            if (kl < sizeof key && decode(eq + 1, (size_t)(e - eq - 1), val, sizeof val)) {
                memcpy(key, qs, kl); key[kl] = 0;
                uint32_t n;
                if (!strcmp(key, "text")) { if (strlen(val) >= sizeof out->f.text) return false; strcpy(out->f.text, val); }
                else if (!strcmp(key, "country")) { if (!to_u32(val, 255, &n)) return false; out->f.country = (uint8_t)n; }
                else if (!strcmp(key, "snr")) { if (!to_u32(val, 99, &n)) return false; out->f.min_snr = (uint8_t)n; }
                else if (!strcmp(key, "free")) { if (!to_u32(val, 1, &n)) return false; out->f.only_free = n != 0; }
                else if (!strcmp(key, "freq")) { if (!to_u32(val, 32000000u, &n)) return false; out->f.freq_hz = n; }
                else if (!strcmp(key, "sort")) { if (!strcmp(val, "snr")) out->f.sort = IRM_DIR_SORT_SNR; else if (!strcmp(val, "list")) out->f.sort = IRM_DIR_SORT_LIST; else return false; }
                else if (!strcmp(key, "offset")) { if (!to_u32(val, 100000, &out->offset)) return false; }
                else if (!strcmp(key, "limit")) { if (!to_u32(val, IRM_WEB_DIR_PAGE_MAX, &n) || n == 0) return false; out->limit = n; }
            } else if (kl < sizeof key) return false;   /* bekannter Schluessellaenge, aber Wert kaputt/zu lang */
        }
        qs = *e ? e + 1 : e;
    }
    return true;
}

/* ---------------------------------------------------------------- JSON */

static void add_clean(cJSON *o, const char *key, const char *src, size_t cap)
{
    char t[64];
    size_t n = strnlen(src, cap < sizeof t ? cap : sizeof t - 1);
    memcpy(t, src, n);
    t[n] = 0;
    irm_web_utf8_clean(t);
    cJSON_AddStringToObject(o, key, t);
}

cJSON *irm_web_dir_list_json(const kiwi_dir_db_t *db, const irm_web_dir_req_t *r)
{
    uint32_t limit = r->limit > IRM_WEB_DIR_PAGE_MAX ? IRM_WEB_DIR_PAGE_MAX : r->limit;
    irm_dir_hit_t *hits = malloc(sizeof(*hits) * (limit ? limit : 1));
    if (!hits) return NULL;
    uint32_t total = 0;
    int n = irm_dir_search(db, &r->f, r->offset, limit, hits, &total);
    cJSON *j = cJSON_CreateObject(), *items = j ? cJSON_AddArrayToObject(j, "items") : NULL;
    if (!j || !items) { cJSON_Delete(j); free(hits); return NULL; }
    if (n < 0) n = 0;
    cJSON_AddNumberToObject(j, "total", total);
    cJSON_AddNumberToObject(j, "offset", r->offset);
    cJSON_AddNumberToObject(j, "limit", limit);
    for (int i = 0; i < n; i++) {
        const kiwi_dir_rec_t *rc = &hits[i].rec;
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddNumberToObject(o, "i", hits[i].idx);
        add_clean(o, "name", rc->name, sizeof rc->name);
        add_clean(o, "place", rc->place, sizeof rc->place);
        char cn[KIWI_DIR_COUNTRY_LEN] = "";
        if (rc->country && kiwi_dir_db_country_name(db, rc->country, cn) != KIWI_DIR_OK) cn[0] = 0;
        add_clean(o, "country", cn, sizeof cn);
        add_clean(o, "host", rc->host, sizeof rc->host);
        cJSON_AddNumberToObject(o, "port", rc->port);
        cJSON_AddNumberToObject(o, "users", rc->users);
        cJSON_AddNumberToObject(o, "max", rc->users_max);
        cJSON_AddNumberToObject(o, "snr", kiwi_dir_rec_snr(rc));
        cJSON_AddNumberToObject(o, "lo", rc->band_lo_hz);
        cJSON_AddNumberToObject(o, "hi", rc->band_hi_hz);
        cJSON_AddItemToArray(items, o);
    }
    free(hits);
    return j;
}

cJSON *irm_web_dir_countries_json(const kiwi_dir_db_t *db)
{
    irm_dir_country_t *c = malloc(sizeof(*c) * 255);
    if (!c) return NULL;
    int n = irm_dir_countries(db, c, 255);
    cJSON *a = cJSON_CreateArray();
    if (!a) { free(c); return NULL; }
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddNumberToObject(o, "id", c[i].id);
        add_clean(o, "name", c[i].name, sizeof c[i].name);
        cJSON_AddNumberToObject(o, "n", c[i].count);
        cJSON_AddItemToArray(a, o);
    }
    free(c);
    return a;
}

cJSON *irm_web_dir_info_json(const irm_dir_info_t *i)
{
    static const char *const st[] = { "idle", "updating", "ok", "failed" };
    cJSON *j = cJSON_CreateObject();
    if (!j) return NULL;
    cJSON_AddBoolToObject(j, "available", i->available);
    cJSON_AddBoolToObject(j, "valid", i->valid);
    cJSON_AddNumberToObject(j, "count", i->count);
    cJSON_AddNumberToObject(j, "generation", i->generation);
    cJSON_AddStringToObject(j, "state", st[i->state <= IRM_DIR_FAILED ? i->state : 0]);
    cJSON_AddNumberToObject(j, "bytes", i->bytes);
    cJSON_AddNumberToObject(j, "servers", i->servers);
    cJSON_AddNumberToObject(j, "err", i->err);
    cJSON_AddStringToObject(j, "msg", irm_dir_err_text(i->err));
    cJSON_AddNumberToObject(j, "retry_in_s", i->retry_in_s);
    return j;
}
