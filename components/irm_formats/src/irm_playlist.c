#include "irm_playlist.h"
#include <stdlib.h>
#include <string.h>

typedef struct { char uri[IRM_PLAYLIST_URI_MAX + 1]; char name[IRM_PLAYLIST_NAME_MAX + 1]; } item_t;

struct irm_playlist { size_t count, max; item_t *items; };

irm_playlist_t *irm_playlist_new(size_t max_items)
{
    if (max_items == 0) return NULL;
    irm_playlist_t *pl = calloc(1, sizeof(*pl));
    if (!pl) return NULL;
    pl->items = calloc(max_items, sizeof(item_t));
    if (!pl->items) { free(pl); return NULL; }
    pl->max = max_items;
    return pl;
}

void irm_playlist_free(irm_playlist_t *pl)
{
    if (!pl) return;
    free(pl->items);
    free(pl);
}

size_t irm_playlist_count(const irm_playlist_t *pl) { return pl ? pl->count : 0; }

const char *irm_playlist_uri(const irm_playlist_t *pl, size_t idx)
{
    return (pl && idx < pl->count) ? pl->items[idx].uri : NULL;
}

const char *irm_playlist_name(const irm_playlist_t *pl, size_t idx)
{
    if (!pl || idx >= pl->count) return NULL;
    return pl->items[idx].name[0] ? pl->items[idx].name : pl->items[idx].uri;
}

bool irm_playlist_add(irm_playlist_t *pl, const char *uri, const char *name)
{
    if (!pl || !uri || pl->count >= pl->max) return false;
    size_t ul = strlen(uri);
    if (ul == 0 || ul > IRM_PLAYLIST_URI_MAX) return false;
    item_t *it = &pl->items[pl->count];
    memcpy(it->uri, uri, ul + 1);
    it->name[0] = 0;
    if (name) { strncpy(it->name, name, IRM_PLAYLIST_NAME_MAX); it->name[IRM_PLAYLIST_NAME_MAX] = 0; }
    pl->count++;
    return true;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
    return s;
}

size_t irm_playlist_parse_m3u(irm_playlist_t *pl, const char *text, size_t len)
{
    if (!pl || !text) return 0;
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text += 3; len -= 3;
    }
    size_t added = 0, pos = 0;
    char line[IRM_PLAYLIST_URI_MAX + IRM_PLAYLIST_NAME_MAX + 32];
    char pending[IRM_PLAYLIST_NAME_MAX + 1] = "";
    while (pos < len) {
        size_t e = pos;
        while (e < len && text[e] != '\n') e++;
        size_t ll = e - pos;
        bool too_long = ll >= sizeof(line);
        if (!too_long) { memcpy(line, text + pos, ll); line[ll] = 0; }
        pos = e + 1;
        if (too_long) { pending[0] = 0; continue; }
        char *s = trim(line);
        if (!*s) continue;
        if (*s == '#') {
            if (strncmp(s, "#EXTINF:", 8) == 0) {
                char *c = strchr(s, ',');
                if (c) { strncpy(pending, trim(c + 1), IRM_PLAYLIST_NAME_MAX); pending[IRM_PLAYLIST_NAME_MAX] = 0; }
            }
            continue;
        }
        if (irm_playlist_add(pl, s, pending[0] ? pending : NULL)) added++;
        pending[0] = 0;
    }
    return added;
}
