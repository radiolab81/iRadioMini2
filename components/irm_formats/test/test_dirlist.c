/* Host-Test: gcc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_dirlist.c src/irm_playlist.c test/test_dirlist.c -o /tmp/tdl && /tmp/tdl */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "irm_dirlist.h"

static int fails;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); else { printf("  FEHLER %s\n", m); fails++; } } while (0)

static void touch(const char *root, const char *rel)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", root, rel);
    FILE *f = fopen(p, "w"); assert(f); fputs("x", f); fclose(f);
}
static void md(const char *root, const char *rel) { char p[512]; snprintf(p, sizeof p, "%s/%s", root, rel); mkdir(p, 0755); }

int main(void)
{
    char root[] = "/tmp/irm_dl_XXXXXX";
    assert(mkdtemp(root));
    md(root, "Rock"); md(root, "alben"); md(root, "Zebra"); md(root, ".versteckt"); md(root, "\xc3\x84rzte");
    touch(root, "b.mp3"); touch(root, "A.MP3"); touch(root, "c.flac"); touch(root, "notiz.txt"); touch(root, ".hidden.mp3"); touch(root, "d.m4a");
    touch(root, "Rock/s1.mp3"); touch(root, "Rock/s2.mp3");
    char longn[130]; memset(longn, 'x', 120); strcpy(longn + 120, ".mp3"); touch(root, longn);

    printf("== Pfade\n");
    char o[256];
    CHECK(irm_path_normalize("/a//b/./c/", o, sizeof o) && !strcmp(o, "/a/b/c"), "normalisiert //, ./ und Endslash");
    CHECK(irm_path_normalize("/", o, sizeof o) && !strcmp(o, "/"), "Wurzel");
    CHECK(irm_path_normalize("//", o, sizeof o) && !strcmp(o, "/"), "nur Slashes = Wurzel");
    CHECK(!irm_path_normalize("/a/../b", o, sizeof o), ".. abgelehnt");
    CHECK(!irm_path_normalize("/..", o, sizeof o), ".. am Ende abgelehnt");
    CHECK(!irm_path_normalize("a/b", o, sizeof o), "ohne fuehrenden Slash abgelehnt");
    CHECK(!irm_path_normalize("/a\\b", o, sizeof o), "Backslash abgelehnt");
    CHECK(!irm_path_normalize("/a\nb", o, sizeof o), "Steuerzeichen abgelehnt");
    CHECK(irm_path_normalize("/.../x", o, sizeof o), "'...' ist ein gueltiger Name");
    CHECK(!irm_path_normalize(NULL, o, sizeof o) && !irm_path_normalize("/a", o, 1), "NULL/zu kleiner Puffer");
    char big[400]; big[0] = 0; for (int i = 0; i < 60; i++) strcat(big, "/abcde");
    CHECK(!irm_path_normalize(big, o, sizeof o), "zu langer Pfad abgelehnt (>200)");
    char seg[300]; seg[0] = '/'; memset(seg + 1, 'a', 120); seg[121] = 0;
    CHECK(!irm_path_normalize(seg, o, sizeof o), "zu langes Segment abgelehnt");
    CHECK(irm_path_strip_root("/sdcard/Rock/s1.mp3", "/sdcard", o, sizeof o) && !strcmp(o, "/Rock/s1.mp3"), "strip_root");
    CHECK(irm_path_strip_root("/sdcard", "/sdcard", o, sizeof o) && !strcmp(o, "/"), "strip_root Wurzel");
    CHECK(!irm_path_strip_root("/sdcardx/a", "/sdcard", o, sizeof o) && !irm_path_strip_root("/tmp/a", "/sdcard", o, sizeof o), "strip_root fremder Pfad");

    printf("== Query\n");
    irm_dl_req_t r;
    CHECK(irm_dirlist_parse_query("", &r) && !strcmp(r.path, "/") && r.limit == 30 && !r.has_after, "Vorgaben");
    CHECK(irm_dirlist_parse_query("path=%2FMusik%2FRock%20%26%20Roll&limit=5", &r) && !strcmp(r.path, "/Musik/Rock & Roll") && r.limit == 5, "Prozentkodierung");
    CHECK(irm_dirlist_parse_query("path=/M%C3%BCnchen", &r) && !strcmp(r.path, "/M\xc3\xbcnchen"), "UTF-8-Umlaut");
    CHECK(irm_dirlist_parse_query("after=d%3AAlben", &r) && r.has_after && r.after_dir && !strcmp(r.after, "Alben"), "Cursor Ordner");
    CHECK(irm_dirlist_parse_query("after=f:x.mp3&foo=bar", &r) && !r.after_dir && !strcmp(r.after, "x.mp3"), "Cursor Datei, unbekannter Schluessel ignoriert");
    CHECK(!irm_dirlist_parse_query("path=/a/../b", &r), "path mit .. -> Fehler");
    CHECK(!irm_dirlist_parse_query("path=%2e%2e%2f", &r) , "kodiertes ../ -> Fehler (kein fuehrender Slash)");
    CHECK(!irm_dirlist_parse_query("path=/%2e%2e/x", &r), "kodiertes .. -> Fehler");
    CHECK(!irm_dirlist_parse_query("path=/a%00b", &r), "%00 -> Fehler");
    CHECK(!irm_dirlist_parse_query("path=/a%2", &r) && !irm_dirlist_parse_query("path=/a%zz", &r), "kaputtes %XX -> Fehler");
    CHECK(!irm_dirlist_parse_query("limit=0", &r) && !irm_dirlist_parse_query("limit=41", &r) && !irm_dirlist_parse_query("limit=x", &r), "limit ausserhalb 1..40 -> Fehler");
    CHECK(!irm_dirlist_parse_query("after=x", &r) && !irm_dirlist_parse_query("after=d:", &r) && !irm_dirlist_parse_query("after=q:a", &r), "ungueltiger Cursor -> Fehler");

    printf("== Listing\n");
    irm_dl_entry_t e[40]; size_t total; bool more;
    int n = irm_dirlist_page(root, "mp3,flac,m4a", IRM_DL_DIRS | IRM_DL_FILES, false, false, NULL, e, 40, &total, &more);
    /* Ordner: alben, Rock, Zebra, Ärzte (UTF-8 hinten) ; Dateien: A.MP3, b.mp3, c.flac, d.m4a  (kein .hidden, kein .txt, kein langer Name) */
    CHECK(n == 8 && total == 8 && !more, "8 Eintraege, nichts Verstecktes/Fremdes/Zulanges");
    const char *want[] = { "alben", "Rock", "Zebra", "\xc3\x84rzte", "A.MP3", "b.mp3", "c.flac", "d.m4a" };
    bool order = n == 8;
    for (int i = 0; i < n && i < 8; i++) if (strcmp(e[i].name, want[i]) || e[i].dir != (i < 4)) order = false;
    CHECK(order, "Reihenfolge: Ordner zuerst, alphabetisch ohne Gross/Klein");

    /* seitenweise mit Cursor, Seitengroesse 3 -> alle Eintraege genau einmal in derselben Reihenfolge */
    char seen[16][IRM_DL_NAME_MAX]; int ns = 0; bool has_after = false, ad = false; char after[IRM_DL_NAME_MAX] = ""; int pages = 0;
    do {
        n = irm_dirlist_page(root, "mp3,flac,m4a", 3, has_after, ad, after, e, 3, &total, &more);
        pages++;
        for (int i = 0; i < n; i++) strcpy(seen[ns++], e[i].name);
        if (n > 0) { has_after = true; ad = e[n - 1].dir; strcpy(after, e[n - 1].name); }
    } while (more && pages < 10);
    bool same = ns == 8 && pages == 3;
    for (int i = 0; same && i < 8; i++) if (strcmp(seen[i], want[i])) same = false;
    CHECK(same, "Cursor-Paging (3 Seiten a 3) liefert alle 8 Eintraege lueckenlos");
    CHECK(total == 8, "total bleibt bei Cursorseiten die Gesamtzahl");
    n = irm_dirlist_page(root, "mp3", IRM_DL_FILES, false, false, NULL, e, 40, &total, &more);
    CHECK(n == 2 && total == 2 && !e[0].dir && !strcmp(e[0].name, "A.MP3") && !strcmp(e[1].name, "b.mp3"), "nur Dateien + Endungsfilter");
    n = irm_dirlist_page(root, "mp3", IRM_DL_DIRS, false, false, NULL, e, 2, &total, &more);
    CHECK(n == 2 && total == 4 && more && e[0].dir && !strcmp(e[1].name, "Rock"), "nur Ordner, limit 2, more=true");
    char sub[300]; snprintf(sub, sizeof sub, "%s/Rock", root);
    n = irm_dirlist_page(sub, "mp3", 3, false, false, NULL, e, 40, &total, &more);
    CHECK(n == 2 && !strcmp(e[0].name, "s1.mp3"), "Unterordner");
    CHECK(irm_dirlist_page("/tmp/gibt_es_nicht_irm", "mp3", 3, false, false, NULL, e, 40, NULL, NULL) == -1, "fehlender Ordner -> -1");
    snprintf(sub, sizeof sub, "%s/b.mp3", root);
    CHECK(irm_dirlist_page(sub, "mp3", 3, false, false, NULL, e, 40, NULL, NULL) == -1, "Datei statt Ordner -> -1");
    CHECK(irm_dirlist_page(root, "mp3", 3, false, false, NULL, e, 0, &total, &more) == 0, "limit 0 -> leer, kein Absturz");

    printf("== Playlist je Ordner\n");
    irm_playlist_t *pl = irm_playlist_new(100);
    CHECK(irm_dirlist_scan_sorted(root, "mp3,flac,m4a", pl) && irm_playlist_count(pl) == 4, "scan_sorted: 4 Dateien (nicht rekursiv)");
    char exp[300]; snprintf(exp, sizeof exp, "%s/A.MP3", root);
    CHECK(!strcmp(irm_playlist_uri(pl, 0), exp) && !strcmp(irm_playlist_name(pl, 3), "d.m4a"), "sortierte URIs/Namen");
    irm_playlist_free(pl);
    pl = irm_playlist_new(2);
    CHECK(irm_dirlist_scan_sorted(root, "mp3,flac,m4a", pl) && irm_playlist_count(pl) == 2, "volle Liste: stoppt sauber bei Kapazitaet");
    irm_playlist_free(pl);
    /* ueber mehrere Seiten (mehr als IRM_DL_PAGE_MAX Dateien) */
    md(root, "viele");
    for (int i = 0; i < 95; i++) { char nm[64]; snprintf(nm, sizeof nm, "viele/t%03d.mp3", i); touch(root, nm); }
    snprintf(sub, sizeof sub, "%s/viele", root);
    pl = irm_playlist_new(300);
    bool ordered = irm_dirlist_scan_sorted(sub, "mp3", pl) && irm_playlist_count(pl) == 95;
    for (size_t i = 0; ordered && i < 95; i++) { char nm[16]; snprintf(nm, sizeof nm, "t%03d.mp3", (int)i); if (strcmp(irm_playlist_name(pl, i), nm)) ordered = false; }
    CHECK(ordered, "95 Dateien ueber 3 Seiten: vollstaendig und sortiert");
    irm_playlist_free(pl);

    printf("== JSON / UTF-8\n");
    irm_dl_entry_t j[3] = { { true, "Rock \"n\" Roll" }, { false, "back\\slash.mp3" }, { false, "bad\xff\x01.mp3" } };
    char *js = irm_dirlist_json("/Mu\"sik", j, 3, 17, true);
    CHECK(js && !strcmp(js, "{\"path\":\"/Mu\\\"sik\",\"total\":17,\"more\":true,\"items\":[{\"n\":\"Rock \\\"n\\\" Roll\",\"d\":1},{\"n\":\"back\\\\slash.mp3\"},{\"n\":\"bad? .mp3\"}]}"), "Escaping, Bereinigung, Format");
    if (js) printf("       %s\n", js);
    free(js);
    js = irm_dirlist_json("/", NULL, 0, 0, false);
    CHECK(js && !strcmp(js, "{\"path\":\"/\",\"total\":0,\"more\":false,\"items\":[]}"), "leere Liste");
    free(js);
    irm_dl_entry_t w[40];
    for (int i = 0; i < 40; i++) { w[i].dir = false; memset(w[i].name, '"', IRM_DL_NAME_MAX - 1); w[i].name[IRM_DL_NAME_MAX - 1] = 0; }
    js = irm_dirlist_json("/", w, 40, 40, false);
    CHECK(js != NULL, "Worst case (40 Namen nur aus Anfuehrungszeichen) passt in den Puffer");
    free(js);
    char u[] = "ok \xc3\xa4\xe2\x82\xac\xf0\x9f\x8e\xb5 \xc0\xaf \xed\xa0\x80 \xf4\x90\x80\x80"; irm_dirlist_utf8_clean(u);
    CHECK(strncmp(u, "ok \xc3\xa4\xe2\x82\xac\xf0\x9f\x8e\xb5 ", 12) == 0 && !strchr(u + 12, '\xc0') && !strchr(u + 12, '\xed'), "UTF-8: gueltige Zeichen bleiben, Overlong/Surrogate/>U+10FFFF -> '?'");

    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", root); if (system(cmd)) {}
    printf("== Ergebnis: %s\n", fails ? "FEHLER" : "OK");
    return fails ? 1 : 0;
}
