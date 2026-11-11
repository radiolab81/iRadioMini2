/* Host-Test: Aufruf siehe README, Abschnitt Tests (braucht zusaetzlich die Quellen von kiwi_core fuer den Modus-Abgleich). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "irm_kiwi_uri.h"
#include "irm_playlist.h"
#include "kiwi_client.h"

static irm_kiwi_uri_t U;
static bool P(const char *s) { memset(&U, 0xAA, sizeof U); return irm_kiwi_uri_parse(s, &U); }

int main(void)
{
    /* --- Grundformen --- */
    assert(P("kiwi://kiwi.example.org:8073/?f=7055.5usb"));
    assert(!strcmp(U.host, "kiwi.example.org") && U.port == 8073 && U.freq_hz == 7055500 && !strcmp(U.mode, "usb") && !U.password[0]);
    assert(P("kiwi://10.0.0.5/?f=225"));                                  /* Standardport kiwi:// = 8073, Modus am */
    assert(U.port == 8073 && U.freq_hz == 225000 && !strcmp(U.mode, "am") && !strcmp(U.host, "10.0.0.5"));
    assert(P("KIWI://Host-1_a.b:9000?f=1008&mode=AMN"));                  /* Schema/Modus ohne Beachtung der Schreibung, kein Pfad */
    assert(U.port == 9000 && U.freq_hz == 1008000 && !strcmp(U.mode, "amn"));
    assert(P("kiwi://h/?mode=lsb&f=3650.123456"));                        /* Reihenfolge egal, Nachkommastellen > 3 abgeschnitten */
    assert(U.freq_hz == 3650123 && !strcmp(U.mode, "lsb"));
    assert(P("kiwi://h/?f=77.5&mode=cw"));                                /* explizites mode= hat Vorrang */
    assert(P("kiwi://h/?f=7100usb&mode=lsb") && !strcmp(U.mode, "lsb"));
    assert(P("  kiwi://h/?f=1"  "000"));                                  /* fuehrende Leerzeichen */

    /* --- aus dem Browser kopiert --- */
    assert(P("http://sdr.example.net:8073/?f=7055.5usbz8"));
    assert(U.port == 8073 && U.freq_hz == 7055500 && !strcmp(U.mode, "usb"));
    assert(P("http://sdr.example.net/?f=198amn&z=5#frag"));
    assert(U.port == 80 && !strcmp(U.mode, "amn") && U.freq_hz == 198000);
    assert(P("ws://sdr.example.net:8074/index.html?foo=1&f=15000sam"));
    assert(U.port == 8074 && U.freq_hz == 15000000 && !strcmp(U.mode, "sam"));
    assert(P("http://h:8073/?f=7055usb/-300,2700") && !strcmp(U.mode, "usb"));   /* Durchlassbereich-Rest ignoriert */

    /* --- Passwort --- */
    assert(P("kiwi://h/?f=1000&pw=geheim"));
    assert(!strcmp(U.password, "geheim"));
    assert(P("kiwi://h/?pw=a%41%2bb&f=1000"));
    assert(!strcmp(U.password, "aA+b"));
    assert(P("kiwi://h/?pw=100%&f=1000") && !strcmp(U.password, "100%"));    /* unvollstaendiges %-Muster bleibt woertlich */
    assert(!P("kiwi://h/?f=1000&pw=0123456789012345678901234567890123456789"));   /* zu lang */

    /* --- Fehler --- */
    assert(!P(NULL));
    assert(!P(""));
    assert(!P("http://radio.example.org/stream.mp3"));                    /* Radio-URL ohne f= */
    assert(!P("ftp://h/?f=1000"));
    assert(!P("kiwi:///?f=1000"));                                        /* kein Host */
    assert(!P("kiwi://h:0/?f=1000"));
    assert(!P("kiwi://h:70000/?f=1000"));
    assert(!P("kiwi://h:80x/?f=1000"));
    assert(!P("kiwi://[::1]/?f=1000"));                                   /* IPv6 nicht unterstuetzt */
    assert(!P("kiwi://user@h/?f=1000"));
    assert(!P("kiwi://h/?f=abc"));
    assert(!P("kiwi://h/?f=5"));                                          /* unter 10 kHz */
    assert(!P("kiwi://h/?f=32001"));                                      /* ueber 32 MHz */
    assert(!P("kiwi://h/?f=1000iq"));                                     /* Stereo-Modus nicht unterstuetzt */
    assert(!P("kiwi://h/?f=1000&mode=drm"));
    assert(!P("kiwi://h/"));                                              /* Frequenz fehlt */
    char longhost[200] = "kiwi://"; memset(longhost + 7, 'a', 100); strcpy(longhost + 107, "/?f=1000");
    assert(!P(longhost));
    assert(P("kiwi://h/?f=32000") && U.freq_hz == 32000000);              /* Grenzen */
    assert(P("kiwi://h/?f=10") && U.freq_hz == 10000);

    /* --- Frequenztext --- */
    char b[16];
    irm_kiwi_fmt_khz(b, sizeof b, 7055000); assert(!strcmp(b, "7055"));
    irm_kiwi_fmt_khz(b, sizeof b, 7055500); assert(!strcmp(b, "7055.5"));
    irm_kiwi_fmt_khz(b, sizeof b, 77500);   assert(!strcmp(b, "77.5"));
    irm_kiwi_fmt_khz(b, sizeof b, 3650123); assert(!strcmp(b, "3650.123"));
    irm_kiwi_fmt_khz(b, sizeof b, 10050);   assert(!strcmp(b, "10.05"));
    irm_kiwi_fmt_khz(b, sizeof b, 1007);    assert(!strcmp(b, "1.007"));
    irm_kiwi_fmt_khz(b, 4, 7055500);        assert(!strcmp(b, "705"));    /* abgeschnitten, aber terminiert */

    /* --- Modus-Tabelle --- */
    assert(irm_kiwi_mode_count() == 14);
    assert(irm_kiwi_mode_name(14) == NULL && !strcmp(irm_kiwi_mode_name(0), "am"));
    assert(irm_kiwi_mode_index("USB") == 8 && irm_kiwi_mode_index("xyz") == -1 && irm_kiwi_mode_index(NULL) == -1 && irm_kiwi_mode_index("") == -1);
    assert(irm_kiwi_mode_index("usbxxxxxxxx") == -1);

    /* --- Abgleich mit kiwi_core: jeder Modus der Tabelle wird vom echten Client akzeptiert ... --- */
    static kiwi_client_t c;
    kiwi_config_t cfg = { .host = "h", .port = 8073, .freq_hz = 225000, .mode = "am" };
    assert(kiwi_client_init(&c, &cfg) == KIWI_OK);
    for (size_t i = 0; i < irm_kiwi_mode_count(); i++) {
        assert(kiwi_client_set_tune(&c, 0, irm_kiwi_mode_name(i)) == KIWI_OK);
        assert(kiwi_client_init(&c, &(kiwi_config_t){ .host = "h", .freq_hz = 225000, .mode = irm_kiwi_mode_name(i) }) == KIWI_OK);
    }
    /* ... und der Client lehnt nach wie vor ab, was wir ablehnen (iq/drm/qam sind Stereo) */
    assert(kiwi_client_set_tune(&c, 0, "iq") != KIWI_OK && kiwi_client_set_tune(&c, 0, "drm") != KIWI_OK);

    /* --- Playlist + Parser zusammen (kiwi.m3u) --- */
    irm_playlist_t *pl = irm_playlist_new(8);
    const char *m3u = "#EXTM3U\r\n#EXTINF:-1,DCF77 Zeitzeichen\r\nkiwi://a.example:8073/?f=77.5&mode=am\r\n#EXTINF:-1,Kaputt\r\nkiwi://b.example/?f=xyz\r\n"
                      "#EXTINF:-1,Kurzwelle\r\nhttp://c.example:8073/?f=6070amz6\r\n";
    assert(irm_playlist_parse_m3u(pl, m3u, strlen(m3u)) == 3);
    assert(P(irm_playlist_uri(pl, 0)) && U.freq_hz == 77500);
    assert(!P(irm_playlist_uri(pl, 1)));
    assert(P(irm_playlist_uri(pl, 2)) && U.freq_hz == 6070000 && !strcmp(U.mode, "am"));
    irm_playlist_free(pl);

    puts("test_kiwi_uri: OK");
    return 0;
}
