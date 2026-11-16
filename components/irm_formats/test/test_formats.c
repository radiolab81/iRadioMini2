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

    /* Eintrag entfernen: Liste im Speicher */
    pl = irm_playlist_new(4);
    irm_playlist_add(pl, "u0", "A"); irm_playlist_add(pl, "u1", "B"); irm_playlist_add(pl, "u2", "C");
    assert(!irm_playlist_remove(pl, 3) && irm_playlist_count(pl) == 3);
    assert(irm_playlist_remove(pl, 1) && irm_playlist_count(pl) == 2);
    assert(!strcmp(irm_playlist_uri(pl, 0), "u0") && !strcmp(irm_playlist_uri(pl, 1), "u2") && !strcmp(irm_playlist_name(pl, 1), "C"));
    assert(irm_playlist_remove(pl, 1) && irm_playlist_remove(pl, 0) && irm_playlist_count(pl) == 0 && !irm_playlist_remove(pl, 0));
    assert(irm_playlist_add(pl, "neu", "N") && !strcmp(irm_playlist_name(pl, 0), "N"));
    irm_playlist_free(pl);

    /* Eintrag entfernen: M3U-Text (URI-Zeile + zugehoerige EXTINF-Zeile, alles andere unveraendert) */
    {
        char t1[512]; size_t l;
        const char *src = "#EXTM3U\r\n# Kommentar\r\n#EXTINF:-1,Eins\r\nkiwi://a/?f=1\r\n\r\n#EXTINF:-1,Zwei\r\nkiwi://b/?f=2\r\nkiwi://kaputt\r\n#EXTINF:-1,Drei\r\nkiwi://c/?f=3";
        strcpy(t1, src); l = strlen(t1);
        assert(irm_m3u_remove_entry(t1, &l, "kiwi://b/?f=2", 0));
        t1[l] = 0;
        assert(!strcmp(t1, "#EXTM3U\r\n# Kommentar\r\n#EXTINF:-1,Eins\r\nkiwi://a/?f=1\r\n\r\nkiwi://kaputt\r\n#EXTINF:-1,Drei\r\nkiwi://c/?f=3"));
        assert(!irm_m3u_remove_entry(t1, &l, "kiwi://b/?f=2", 0) && !irm_m3u_remove_entry(t1, &l, "kiwi://nix", 0));
        assert(irm_m3u_remove_entry(t1, &l, "kiwi://c/?f=3", 0));   /* letzte Zeile ohne Zeilenende */
        t1[l] = 0;
        assert(!strcmp(t1, "#EXTM3U\r\n# Kommentar\r\n#EXTINF:-1,Eins\r\nkiwi://a/?f=1\r\n\r\nkiwi://kaputt\r\n"));
        assert(irm_m3u_remove_entry(t1, &l, "kiwi://a/?f=1", 0));
        t1[l] = 0;
        assert(!strcmp(t1, "#EXTM3U\r\n# Kommentar\r\n\r\nkiwi://kaputt\r\n"));   /* EXTINF des Eintrags mit entfernt, Fremdes bleibt */
        /* Duplikate: nth waehlt das Vorkommen; EXTINF gehoert nur zum jeweils eigenen Eintrag */
        strcpy(t1, "#EXTINF:-1,X1\nu\n#EXTINF:-1,X2\nu\nv\n"); l = strlen(t1);
        assert(irm_m3u_remove_entry(t1, &l, "u", 1)); t1[l] = 0;
        assert(!strcmp(t1, "#EXTINF:-1,X1\nu\nv\n"));
        /* BOM, Leerzeichen um die URI, EXTINF ohne Komma gehoert nicht dazu, URI als erste Zeile */
        strcpy(t1, "\xEF\xBB\xBF  kiwi://z  \n#EXTINF:-1,Name\nq\n#EXTINF:5\nw\n"); l = strlen(t1);
        assert(irm_m3u_remove_entry(t1, &l, "kiwi://z", 0)); t1[l] = 0;
        assert(!strcmp(t1, "\xEF\xBB\xBF#EXTINF:-1,Name\nq\n#EXTINF:5\nw\n"));
        assert(irm_m3u_remove_entry(t1, &l, "w", 0)); t1[l] = 0;
        assert(!strcmp(t1, "\xEF\xBB\xBF#EXTINF:-1,Name\nq\n#EXTINF:5\n"));
        assert(!irm_m3u_remove_entry(t1, &l, "", 0) && !irm_m3u_remove_entry(NULL, &l, "q", 0));
        /* Ergebnis wird vom Parser wieder gelesen */
        strcpy(t1, "#EXTM3U\n#EXTINF:-1,A\nu1\n#EXTINF:-1,B\nu2\n#EXTINF:-1,C\nu3\n"); l = strlen(t1);
        assert(irm_m3u_remove_entry(t1, &l, "u2", 0));
        pl = irm_playlist_new(8);
        assert(irm_playlist_parse_m3u(pl, t1, l) == 2 && !strcmp(irm_playlist_name(pl, 0), "A") && !strcmp(irm_playlist_name(pl, 1), "C"));
        irm_playlist_free(pl);
    }
    puts("irm_formats: alle Tests ok");
    return 0;
}
