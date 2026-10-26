#include "irm_eqfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool irm_eqfile_parse(const char *text, size_t len, int8_t *out, size_t n, int lo, int hi)
{
    if (!text || !out) return false;
    size_t got = 0, pos = 0;
    char line[32];
    while (pos < len && got < n) {
        size_t e = pos;
        while (e < len && text[e] != '\n') e++;
        size_t ll = e - pos; if (ll >= sizeof(line)) ll = sizeof(line) - 1;
        memcpy(line, text + pos, ll); line[ll] = 0;
        pos = e + 1;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        char *end;
        long v = strtol(s, &end, 10);
        if (end == s) return false;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        out[got++] = (int8_t)v;
    }
    return got == n;
}

size_t irm_eqfile_format(const int8_t *vals, size_t n, char *buf, size_t buf_sz)
{
    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        int w = snprintf(buf + off, buf_sz - off, "%d\n", vals[i]);
        if (w < 0 || (size_t)w >= buf_sz - off) return 0;
        off += (size_t)w;
    }
    return off;
}
