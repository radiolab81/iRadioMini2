/* Host-Test: Aufruf siehe README, Abschnitt Tests */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "irm_playlist.h"
#include "irm_wifi_conf.h"
#include "irm_eqfile.h"

int main(void)
{
    irm_playlist_t *pl = irm_playlist_new(4);
    const char *m3u = "\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:-1,Soma FM\r\nhttp://a/b\r\n\r\n  http://c/d  \n# kommentar\nhttp://e\nhttp://f\nhttp://g\n";
    assert(irm_playlist_parse_m3u(pl, m3u, strlen(m3u)) == 4);
    assert(strcmp(irm_playlist_name(pl, 0), "Soma FM") == 0);
    assert(strcmp(irm_playlist_uri(pl, 1), "http://c/d") == 0);
    assert(strcmp(irm_playlist_name(pl, 1), "http://c/d") == 0);
    assert(irm_playlist_uri(pl, 4) == NULL);
    irm_playlist_free(pl);

    char ssid[33], psk[65];
    const char *w = "ctrl_interface=x\r\nnetwork={\r\n  bssid=\"aa\"\r\n  ssid=\"Mein Netz\"\r\n  psk=\"geheim123\"\r\n}\r\n";
    assert(irm_wifi_conf_parse(w, strlen(w), ssid, sizeof ssid, psk, sizeof psk));
    assert(!strcmp(ssid, "Mein Netz") && !strcmp(psk, "geheim123"));
    const char *o = "network={\n ssid=\"Cafe\"\n key_mgmt=NONE\n}\n";
    assert(irm_wifi_conf_parse(o, strlen(o), ssid, sizeof ssid, psk, sizeof psk) && !psk[0]);
    assert(!irm_wifi_conf_parse("", 0, ssid, sizeof ssid, psk, sizeof psk));

    int8_t eq[3]; const char *e = "1\n# x\n-30\n99\n";
    assert(irm_eqfile_parse(e, strlen(e), eq, 3, -12, 12) && eq[0]==1 && eq[1]==-12 && eq[2]==12);
    assert(!irm_eqfile_parse("1\n", 2, eq, 3, -12, 12));
    char b[32]; assert(irm_eqfile_format(eq, 3, b, sizeof b) == strlen("1\n-12\n12\n"));
    puts("irm_formats: alle Tests ok");
    return 0;
}
