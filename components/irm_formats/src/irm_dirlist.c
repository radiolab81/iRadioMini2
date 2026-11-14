#include "irm_dirlist.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- Pfade */

bool irm_path_normalize(const char *in, char *out, size_t cap)
{
    if (!in || !out || cap < 2 || in[0] != '/') return false;
    size_t o = 0;
    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != '/') {
            unsigned char c = (unsigned char)*e;
            if (c < 0x20 || c == 0x7F || c == '\\') return false;
            e++;
        }
        const size_t l = (size_t)(e - p);
        if (l >= IRM_DL_NAME_MAX) return false;
        if (l == 1 && p[0] == '.') { p = e; continue; }
        if (l == 2 && p[0] == '.' && p[1] == '.') return false;
        if (o + 1 + l + 1 > cap || o + 1 + l > IRM_DL_PATH_MAX) return false;
        out[o++] = '/';
        memcpy(out + o, p, l);
        o += l;
        p = e;
    }
    if (o == 0) out[o++] = '/';
    out[o] = 0;
    return true;
}

bool irm_path_strip_root(const char *uri, const char *root, char *out, size_t cap)
{
    if (!uri || !root || !out || cap < 2) return false;
    const size_t rl = strlen(root);
    if (strncmp(uri, root, rl) != 0) return false;
    uri += rl;
    if (*uri == 0) { strcpy(out, "/"); return true; }
    if (*uri != '/') return false;
    return irm_path_normalize(uri, out, cap);
}

bool irm_dirlist_ext_match(const char *name, const char *exts)
{
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1]) return false;
    dot++;
    const size_t dl = strlen(dot);
    const char *p = exts;
    while (p && *p) {
        const char *e = strchr(p, ',');
        const size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == dl && strncasecmp(dot, p, l) == 0) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

/* ---------------------------------------------------------------- UTF-8 / JSON */

size_t irm_dirlist_utf8_clean(char *s)
{
    unsigned char *p = (unsigned char *)s;
    size_t i = 0;
    while (p[i]) {
        unsigned char c = p[i];
        size_t need = 0;
        unsigned char lo = 0x80, hi = 0xBF;
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

typedef struct { char *b; size_t n, cap; bool ok; } sb_t;

static void sb_raw(sb_t *s, const char *t)
{
    const size_t l = strlen(t);
    if (!s->ok || s->n + l + 1 > s->cap) { s->ok = false; return; }
    memcpy(s->b + s->n, t, l + 1);
    s->n += l;
}

/* t wird bereinigt (Kopie) und als JSON-String-Inhalt angehaengt */
static void sb_str(sb_t *s, const char *t)
{
    char tmp[IRM_DL_PATH_MAX + 2];
    size_t l = strnlen(t, sizeof tmp - 1);
    memcpy(tmp, t, l);
    tmp[l] = 0;
    irm_dirlist_utf8_clean(tmp);
    for (const char *p = tmp; *p; p++) {
        if (!s->ok || s->n + 3 > s->cap) { s->ok = false; return; }
        if (*p == '"' || *p == '\\') s->b[s->n++] = '\\';
        s->b[s->n++] = *p;
    }
    s->b[s->n] = 0;
}

char *irm_dirlist_json(const char *rel_path, const irm_dl_entry_t *e, size_t n, size_t total, bool more)
{
    const size_t cap = 160 + 2 * strlen(rel_path) + n * (2 * IRM_DL_NAME_MAX + 24);
    sb_t s = { malloc(cap), 0, cap, true };
    if (!s.b) return NULL;
    s.b[0] = 0;
    char num[48];
    sb_raw(&s, "{\"path\":\""); sb_str(&s, rel_path);
    snprintf(num, sizeof num, "\",\"total\":%u,\"more\":%s,\"items\":[", (unsigned)total, more ? "true" : "false");
    sb_raw(&s, num);
    for (size_t i = 0; i < n; i++) {
        sb_raw(&s, i ? ",{\"n\":\"" : "{\"n\":\"");
        sb_str(&s, e[i].name);
        sb_raw(&s, e[i].dir ? "\",\"d\":1}" : "\"}");
    }
    sb_raw(&s, "]}");
    if (!s.ok) { free(s.b); return NULL; }
    return s.b;
}

/* ---------------------------------------------------------------- Query */

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode(const char *v, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = v[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= n) return false;
            const int h = hexv(v[i + 1]), l = hexv(v[i + 2]);
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

bool irm_dirlist_parse_query(const char *qs, irm_dl_req_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    out->limit = IRM_DL_PAGE_DEFAULT;
    strcpy(out->path, "/");
    while (qs && *qs) {
        const char *e = qs;
        while (*e && *e != '&') e++;
        const char *eq = memchr(qs, '=', (size_t)(e - qs));
        if (eq) {
            const size_t kl = (size_t)(eq - qs);
            char key[8];
            if (kl < sizeof key) {
                memcpy(key, qs, kl); key[kl] = 0;
                char val[IRM_DL_PATH_MAX + 8];
                const bool known = !strcmp(key, "path") || !strcmp(key, "after") || !strcmp(key, "limit");
                if (known && !decode(eq + 1, (size_t)(e - eq - 1), val, sizeof val)) return false;
                if (!strcmp(key, "path")) {
                    if (!irm_path_normalize(val, out->path, sizeof out->path)) return false;
                } else if (!strcmp(key, "after")) {
                    if ((val[0] != 'd' && val[0] != 'f') || val[1] != ':' || !val[2] || strlen(val + 2) >= IRM_DL_NAME_MAX) return false;
                    out->has_after = true; out->after_dir = val[0] == 'd';
                    strcpy(out->after, val + 2);
                } else if (!strcmp(key, "limit")) {
                    char *end;
                    const long l = strtol(val, &end, 10);
                    if (*end || l < 1 || l > IRM_DL_PAGE_MAX) return false;
                    out->limit = (uint32_t)l;
                }
            }
        }
        qs = *e ? e + 1 : e;
    }
    return true;
}

/* ---------------------------------------------------------------- Listing */

static int cmp_key(bool ad, const char *an, bool bd, const char *bn)
{
    if (ad != bd) return ad ? -1 : 1;     /* Ordner vor Dateien */
    const int c = strcasecmp(an, bn);
    return c ? c : strcmp(an, bn);
}

static bool is_dir(const char *abs_dir, const struct dirent *de, bool *dir)
{
#ifdef DT_DIR
    if (de->d_type == DT_DIR) { *dir = true; return true; }
    if (de->d_type == DT_REG) { *dir = false; return true; }
#endif
    char path[IRM_PLAYLIST_URI_MAX + IRM_DL_NAME_MAX + 2];
    const int w = snprintf(path, sizeof path, "%s/%s", abs_dir, de->d_name);
    struct stat st;
    if (w < 0 || (size_t)w >= sizeof path || stat(path, &st) != 0) return false;
    *dir = S_ISDIR(st.st_mode);
    return S_ISDIR(st.st_mode) || S_ISREG(st.st_mode);
}

int irm_dirlist_page(const char *abs_dir, const char *exts, unsigned flags, bool has_after, bool after_dir, const char *after,
                     irm_dl_entry_t *out, size_t limit, size_t *total, bool *more)
{
    if (limit == 0) return 0;
    DIR *d = opendir(abs_dir);
    if (!d) return -1;
    size_t n = 0, tot = 0, rest = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || strlen(de->d_name) >= IRM_DL_NAME_MAX) continue;
        bool dir;
        if (!is_dir(abs_dir, de, &dir)) continue;
        if (dir ? !(flags & IRM_DL_DIRS) : (!(flags & IRM_DL_FILES) || !irm_dirlist_ext_match(de->d_name, exts))) continue;
        tot++;
        if (has_after && cmp_key(dir, de->d_name, after_dir, after) <= 0) continue;
        rest++;
        if (n == limit && cmp_key(dir, de->d_name, out[n - 1].dir, out[n - 1].name) >= 0) continue;
        size_t pos = n < limit ? n : n - 1;   /* ist die Seite voll, faellt der groesste Eintrag heraus */
        while (pos > 0 && cmp_key(dir, de->d_name, out[pos - 1].dir, out[pos - 1].name) < 0) { out[pos] = out[pos - 1]; pos--; }
        out[pos].dir = dir;
        strcpy(out[pos].name, de->d_name);
        if (n < limit) n++;
    }
    closedir(d);
    if (total) *total = tot;
    if (more) *more = rest > n;
    return (int)n;
}

bool irm_dirlist_scan_sorted(const char *abs_dir, const char *exts, irm_playlist_t *pl)
{
    irm_dl_entry_t *page = malloc(IRM_DL_PAGE_MAX * sizeof *page);
    if (!page) return false;
    bool has_after = false, more = true, ok = true;
    char after[IRM_DL_NAME_MAX] = "";
    while (more && ok) {
        const int n = irm_dirlist_page(abs_dir, exts, IRM_DL_FILES, has_after, false, after, page, IRM_DL_PAGE_MAX, NULL, &more);
        if (n < 0) { ok = false; break; }
        for (int i = 0; i < n; i++) {
            char uri[IRM_PLAYLIST_URI_MAX + 1];
            const int w = snprintf(uri, sizeof uri, "%s/%s", abs_dir, page[i].name);
            if (w < 0 || (size_t)w >= sizeof uri) continue;
            if (!irm_playlist_add(pl, uri, page[i].name)) { free(page); return true; }   /* Liste voll */
        }
        if (n == 0) break;
        has_after = true;
        strcpy(after, page[n - 1].name);
    }
    free(page);
    return ok;
}
