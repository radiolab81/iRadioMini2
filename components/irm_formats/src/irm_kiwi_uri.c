#include "irm_kiwi_uri.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Reihenfolge und Namen wie k_modes[] in kiwi_core/src/kiwi_client.c (dort static; test_kiwi_uri.c prueft die Uebereinstimmung). */
static const char *const k_modes[] = { "am", "amn", "amw", "sam", "sal", "sau", "lsb", "lsn", "usb", "usn", "cw", "cwn", "nbfm", "nnfm" };
#define N_MODES (sizeof(k_modes) / sizeof(k_modes[0]))

size_t irm_kiwi_mode_count(void) { return N_MODES; }
const char *irm_kiwi_mode_name(size_t idx) { return idx < N_MODES ? k_modes[idx] : NULL; }

/* name[0..len) in Kleinbuchstaben nachschlagen */
static int mode_index_n(const char *name, size_t len)
{
    if (!name || len == 0 || len >= IRM_KIWI_MODE_MAX) return -1;
    char t[IRM_KIWI_MODE_MAX];
    for (size_t i = 0; i < len; i++) t[i] = (char)tolower((unsigned char)name[i]);
    t[len] = 0;
    for (size_t k = 0; k < N_MODES; k++) if (strcmp(t, k_modes[k]) == 0) return (int)k;
    return -1;
}

int irm_kiwi_mode_index(const char *name) { return name ? mode_index_n(name, strlen(name)) : -1; }

void irm_kiwi_fmt_khz(char *out, size_t n, uint32_t hz)
{
    if (!out || !n) return;
    unsigned khz = hz / 1000u, frac = hz % 1000u;
    if (!frac) { snprintf(out, n, "%u", khz); return; }
    char f[4];
    snprintf(f, sizeof f, "%03u", frac);
    for (int i = 2; i > 0 && f[i] == '0'; i--) f[i] = 0;
    snprintf(out, n, "%u.%s", khz, f);
}

/* ---- URI-Zerlegung ---- */

static size_t prefix_ci(const char *s, const char *lit)
{
    size_t n = strlen(lit);
    for (size_t i = 0; i < n; i++) if (tolower((unsigned char)s[i]) != lit[i]) return 0;   /* s endet vor lit -> NUL != lit[i] */
    return n;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* f=<kHz>[<modus>][z<zoom>] : Zahl mit bis zu 3 Nachkommastellen (weitere werden abgeschnitten), danach optional der Modus. */
static bool parse_f(const char *v, size_t len, uint32_t *hz, int *mode_idx)
{
    size_t i = 0;
    uint32_t ip = 0, frac = 0, digits = 0;
    bool any = false;
    for (; i < len && isdigit((unsigned char)v[i]); i++) {
        any = true;
        ip = ip * 10u + (uint32_t)(v[i] - '0');
        if (ip > 100000u) return false;
    }
    if (i < len && v[i] == '.') {
        for (i++; i < len && isdigit((unsigned char)v[i]); i++) {
            any = true;
            if (digits < 3) { frac = frac * 10u + (uint32_t)(v[i] - '0'); digits++; }
        }
    }
    if (!any) return false;
    while (digits < 3) { frac *= 10u; digits++; }
    *hz = ip * 1000u + frac;
    *mode_idx = -1;
    size_t a = i;
    while (i < len && isalpha((unsigned char)v[i])) i++;
    size_t alen = i - a;
    if (alen) {
        /* "usbz8": das 'z' gehoert zur Zoom-Angabe, wenn Ziffern folgen */
        if (v[a + alen - 1] == 'z' && i < len && isdigit((unsigned char)v[i])) alen--;
        *mode_idx = mode_index_n(v + a, alen);
        if (*mode_idx < 0) return false;
    }
    return true;   /* Rest (Zoom, Durchlassbereich ...) wird ignoriert */
}

bool irm_kiwi_uri_parse(const char *uri, irm_kiwi_uri_t *out)
{
    if (!uri || !out) return false;
    while (*uri == ' ' || *uri == '\t') uri++;
    uint16_t default_port;
    size_t sl;
    if ((sl = prefix_ci(uri, "kiwi://"))) default_port = 8073;
    else if ((sl = prefix_ci(uri, "http://")) || (sl = prefix_ci(uri, "ws://"))) default_port = 80;
    else return false;
    const char *p = uri + sl;

    irm_kiwi_uri_t r;
    memset(&r, 0, sizeof r);
    size_t hl = 0;
    while (p[hl] && (isalnum((unsigned char)p[hl]) || p[hl] == '.' || p[hl] == '-' || p[hl] == '_')) hl++;
    if (hl == 0 || hl >= IRM_KIWI_HOST_MAX) return false;
    memcpy(r.host, p, hl);
    p += hl;
    r.port = default_port;
    if (*p == ':') {
        p++;
        unsigned v = 0; int nd = 0;
        while (isdigit((unsigned char)*p)) { v = v * 10u + (unsigned)(*p - '0'); p++; if (++nd > 5) return false; }
        if (nd == 0 || v == 0 || v > 65535u) return false;
        r.port = (uint16_t)v;
    }
    if (*p && *p != '/' && *p != '?' && *p != '#') return false;   /* z. B. IPv6-Literal oder Benutzer@Host */
    while (*p && *p != '?' && *p != '#') p++;                      /* Pfad wird nicht ausgewertet */

    bool have_f = false;
    int f_mode = -1, mode_param = -1;
    uint32_t hz = 0;
    if (*p == '?') {
        p++;
        while (*p && *p != '#') {
            const char *e = p;
            while (*e && *e != '&' && *e != '#') e++;
            const char *eq = memchr(p, '=', (size_t)(e - p));
            if (eq) {
                size_t kl = (size_t)(eq - p), vl = (size_t)(e - eq - 1);
                const char *v = eq + 1;
                if (kl == 1 && p[0] == 'f') {
                    if (!parse_f(v, vl, &hz, &f_mode)) return false;
                    have_f = true;
                } else if (kl == 4 && strncmp(p, "mode", 4) == 0) {
                    mode_param = mode_index_n(v, vl);
                    if (mode_param < 0) return false;
                } else if (kl == 2 && strncmp(p, "pw", 2) == 0) {
                    size_t o = 0;
                    for (size_t i = 0; i < vl; i++) {
                        char c = v[i];
                        if (c == '%' && i + 2 < vl && hexval(v[i + 1]) >= 0 && hexval(v[i + 2]) >= 0) {   /* %XX */
                            c = (char)(hexval(v[i + 1]) * 16 + hexval(v[i + 2]));
                            i += 2;
                        }
                        if (o + 1 >= sizeof r.password || c == 0) return false;
                        r.password[o++] = c;
                    }
                    r.password[o] = 0;
                }
            }
            p = *e == '&' ? e + 1 : e;
        }
    }
    if (!have_f || hz < IRM_KIWI_FREQ_MIN_HZ || hz > IRM_KIWI_FREQ_MAX_HZ) return false;
    int mi = mode_param >= 0 ? mode_param : (f_mode >= 0 ? f_mode : 0);   /* Standard: am */
    strcpy(r.mode, k_modes[mi]);
    r.freq_hz = hz;
    *out = r;
    return true;
}

bool irm_kiwi_uri_build(const irm_kiwi_uri_t *u, char *out, size_t n)
{
    if (!u || !out || n == 0) return false;
    if (!u->host[0] || u->port == 0 || u->freq_hz < IRM_KIWI_FREQ_MIN_HZ || u->freq_hz > IRM_KIWI_FREQ_MAX_HZ || irm_kiwi_mode_index(u->mode) < 0) return false;
    for (const char *h = u->host; *h; h++) if (!(isalnum((unsigned char)*h) || *h == '.' || *h == '-' || *h == '_')) return false;
    char f[16];
    irm_kiwi_fmt_khz(f, sizeof f, u->freq_hz);
    int len = snprintf(out, n, "kiwi://%s:%u/?f=%s&mode=%s", u->host, (unsigned)u->port, f, u->mode);
    if (len < 0 || (size_t)len >= n) return false;
    if (u->password[0]) {
        static const char hex[] = "0123456789ABCDEF";
        size_t o = (size_t)len;
        const char *k = "&pw=";
        for (; *k; k++) { if (o + 1 >= n) return false; out[o++] = *k; }
        for (const char *p = u->password; *p; p++) {
            unsigned char ch = (unsigned char)*p;
            if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') { if (o + 1 >= n) return false; out[o++] = (char)ch; }
            else { if (o + 3 >= n) return false; out[o++] = '%'; out[o++] = hex[ch >> 4]; out[o++] = hex[ch & 15]; }
        }
        out[o] = 0;
    }
    return true;
}
