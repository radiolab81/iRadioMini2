#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "irm_files.h"

static const char *TAG = "irm_files";

esp_err_t irm_files_read(const char *path, size_t max_size, char **buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || (size_t)sz > max_size) { fclose(f); return ESP_ERR_INVALID_SIZE; }
    char *b = malloc((size_t)sz + 1);
    if (!b) { fclose(f); return ESP_ERR_NO_MEM; }
    size_t n = fread(b, 1, (size_t)sz, f);
    fclose(f);
    b[n] = 0;
    *buf = b;
    if (len) *len = n;
    return ESP_OK;
}

esp_err_t irm_files_write(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return ESP_FAIL;
    size_t n = fwrite(data, 1, len, f);
    int rc = fclose(f);
    return (n == len && rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t irm_files_load_playlist(const char *path, irm_playlist_t *pl)
{
    char *txt; size_t len;
    esp_err_t e = irm_files_read(path, 64 * 1024, &txt, &len);
    if (e != ESP_OK) return e;
    size_t n = irm_playlist_parse_m3u(pl, txt, len);
    free(txt);
    ESP_LOGI(TAG, "%u Eintraege aus %s", (unsigned)n, path);
    return n ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static bool ext_match(const char *name, const char *exts)
{
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1]) return false;
    dot++;
    size_t dl = strlen(dot);
    const char *p = exts;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == dl && strncasecmp(dot, p, l) == 0) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

static void scan(const char *dir, const char *exts, int depth, irm_playlist_t *pl)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    char path[IRM_PLAYLIST_URI_MAX + 1];
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        int w = snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        if (w < 0 || (size_t)w >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0) scan(path, exts, depth - 1, pl);
        } else if (ext_match(de->d_name, exts)) {
            if (!irm_playlist_add(pl, path, de->d_name)) { closedir(d); return; }
        }
    }
    closedir(d);
}

esp_err_t irm_files_scan_media(const char *root, const char *extensions, int max_depth, irm_playlist_t *pl)
{
    scan(root, extensions, max_depth, pl);
    return irm_playlist_count(pl) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
