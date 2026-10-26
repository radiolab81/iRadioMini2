#include "irm_wifi_conf.h"
#include <string.h>
#include <strings.h>

static void copy_value(const char *v, char *dst, size_t sz)
{
    size_t n = strlen(v);
    if (n >= 2 && v[0] == '"' && v[n-1] == '"') { v++; n -= 2; }
    else if (n >= 1 && v[0] == '"') { v++; n--; }
    if (n >= sz) n = sz - 1;
    memcpy(dst, v, n);
    dst[n] = 0;
}

bool irm_wifi_conf_parse(const char *text, size_t len, char *ssid, size_t ssid_sz, char *psk, size_t psk_sz)
{
    if (!text || !ssid || !psk || !ssid_sz || !psk_sz) return false;
    ssid[0] = 0; psk[0] = 0;
    bool open_net = false, have = false;
    size_t pos = 0;
    char line[160];
    while (pos < len) {
        size_t e = pos;
        while (e < len && text[e] != '\n') e++;
        size_t ll = e - pos; if (ll >= sizeof(line)) ll = sizeof(line) - 1;
        memcpy(line, text + pos, ll); line[ll] = 0;
        pos = e + 1;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t n = strlen(s);
        while (n && (s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
        if (!*s || *s == '#') continue;
        if (strncmp(s, "network", 7) == 0 && strchr(s, '{')) {
            if (have) break;
            ssid[0] = 0; psk[0] = 0; open_net = false; continue;
        }
        if (s[0] == '}') { if (have) break; continue; }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = s, *val = eq + 1;
        size_t kl = strlen(key);
        while (kl && (key[kl-1] == ' ' || key[kl-1] == '\t')) key[--kl] = 0;
        /* exakter Schluesselvergleich (Altcode fand per strstr auch "bssid=") */
        if (strcasecmp(key, "ssid") == 0) { copy_value(val, ssid, ssid_sz); have = ssid[0] != 0; }
        else if (strcasecmp(key, "psk") == 0) copy_value(val, psk, psk_sz);
        else if (strcasecmp(key, "key_mgmt") == 0 && strncasecmp(val, "NONE", 4) == 0) open_net = true;
    }
    if (open_net) psk[0] = 0;
    return have;
}
