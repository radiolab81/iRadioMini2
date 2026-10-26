#include <stdio.h>
#include <string.h>
#include "irm_display.h"

void irm_view_title(const irm_status_t *st, char *out, size_t n)
{
    if (!st->net_up && st->source == IRM_SOURCE_RADIO) snprintf(out, n, "Kein WLAN");
    else if (st->index < 0) snprintf(out, n, st->source == IRM_SOURCE_RADIO ? "Keine Sender" : "Keine Dateien");
    else snprintf(out, n, "%s", st->name[0] ? st->name : st->uri);
}

void irm_view_info(const irm_status_t *st, char *out, size_t n)
{
    static const char *const ps[] = { "STOP", "CONN", "PLAY", "PAUSE", "FEHLER" };
    int len = snprintf(out, n, "%s", ps[st->player <= IRM_PLAYER_ERROR ? st->player : 0]);
    if (len > 0 && (size_t)len < n && st->player == IRM_PLAYER_PLAYING && st->sample_rate)
        len += snprintf(out + len, n - (size_t)len, " %lu.%luk", (unsigned long)(st->sample_rate / 1000), (unsigned long)(st->sample_rate % 1000 / 100));
    if (len > 0 && (size_t)len < n && st->player == IRM_PLAYER_PLAYING && st->bitrate)
        snprintf(out + len, n - (size_t)len, " %luk", (unsigned long)(st->bitrate / 1000));
}

void irm_view_volume(const irm_status_t *st, char *out, size_t n) { snprintf(out, n, "Vol %u%%", st->volume); }
