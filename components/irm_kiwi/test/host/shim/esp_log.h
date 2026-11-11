#pragma once
#include <stdio.h>
#include <string.h>
#define ESP_LOGI(t, f, ...) printf("I (%s) " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) printf("W (%s) " f "\n", t, ##__VA_ARGS__)
#define ESP_LOGE(t, f, ...) printf("E (%s) " f "\n", t, ##__VA_ARGS__)
/* strlcpy gibt es in newlib (ESP-IDF); neuere glibc deklariert es ebenfalls. Eigener Name + Makro vermeidet jede Kollision. */
#include <stddef.h>
static inline size_t irm_shim_strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; } return l; }
#define strlcpy irm_shim_strlcpy
