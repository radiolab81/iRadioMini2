/* Host-Test: Query-Parser, UTF-8-Bereinigung und JSON des Serververzeichnisses (echtes cJSON, echte Datenbank aus ../irm_dir/test/kiwisdr_sample.js).
 * Aufruf siehe README, Abschnitt Tests. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "irm_web_dir.h"
#include "kiwi_dir_update.h"

static int fails;
#define CHECK(c, ...) do { int ok_ = !!(c); printf("  %s ", ok_ ? "ok  " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

#define SLOT (192u * 1024u)
static uint8_t g_mem[KIWI_DIR_SLOTS][SLOT];
static int m_read(void *c, int s, size_t o, void *b, size_t n) { (void)c; if (o + n > SLOT) return -1; memcpy(b, g_mem[s] + o, n); return 0; }
static int m_erase(void *c, int s) { (void)c; memset(g_mem[s], 0xFF, SLOT); return 0; }
static int m_write(void *c, int s, size_t o, const void *b, size_t n) { (void)c; if (o + n > SLOT) return -1; memcpy(g_mem[s] + o, b, n); return 0; }
static const kiwi_dir_store_t g_store = { SLOT, 4096, m_read, m_erase, m_write, NULL };

/* unabhaengiger, strikter UTF-8-Validator (RFC 3629) */
static int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        int n = (*p >= 0xC2 && *p <= 0xDF) ? 1 : (*p >= 0xE0 && *p <= 0xEF) ? 2 : (*p >= 0xF0 && *p <= 0xF4) ? 3 : -1;
        if (n < 0) return 0;
        unsigned cp = n == 1 ? (*p & 0x1F) : n == 2 ? (*p & 0x0F) : (*p & 0x07);
        for (int k = 1; k <= n; k++) { if ((p[k] & 0xC0) != 0x80) return 0; cp = (cp << 6) | (p[k] & 0x3F); }
        if ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return 0;
        p += n + 1;
    }
    return 1;
}
static const char *C(const char *s) { static char b[256]; snprintf(b, sizeof b, "%s", s); irm_web_utf8_clean(b); return b; }

/* needle in buf durch repl ersetzen (buf hat cap Bytes) */
static int replace_in(char *buf, size_t cap, const char *needle, const char *repl)
{
    char *p = strstr(buf, needle);
    if (!p || strlen(buf) - strlen(needle) + strlen(repl) + 1 > cap) return 0;
    memmove(p + strlen(repl), p + strlen(needle), strlen(p + strlen(needle)) + 1);
    memcpy(p, repl, strlen(repl));
    return 1;
}

int main(int argc, char **argv)
{
    printf("== UTF-8-Bereinigung\n");
    CHECK(!strcmp(C("Hallo"), "Hallo"), "ASCII unveraendert");
    CHECK(!strcmp(C("Gro\xc3\x9f \xe2\x82\xac \xf0\x9f\x93\xa1"), "Gro\xc3\x9f \xe2\x82\xac \xf0\x9f\x93\xa1"), "gueltige 2-/3-/4-Byte-Zeichen bleiben");
    CHECK(!strcmp(C("ab\xc3"), "ab?"), "abgeschnittenes 2-Byte-Zeichen am Ende -> ?");
    CHECK(!strcmp(C("a\xe2\x82"), "a??"), "abgeschnittenes 3-Byte-Zeichen -> ??");
    CHECK(!strcmp(C("a\xc3" "b"), "a?b"), "Startbyte ohne Folgebyte mitten im Text");
    CHECK(!strcmp(C("a\x80z"), "a?z") && !strcmp(C("\xff\xfe"), "??"), "einzelne Fortsetzungs-/Ungueltigbytes");
    CHECK(!strcmp(C("\xc0\xaf"), "??") && !strcmp(C("\xe0\x80\x80"), "???"), "Overlong-Kodierungen abgelehnt");
    CHECK(!strcmp(C("\xed\xa0\x80"), "???"), "UTF-16-Surrogate abgelehnt");
    CHECK(!strcmp(C("\xf4\x90\x80\x80"), "????"), "Codepoints > U+10FFFF abgelehnt");
    CHECK(!strcmp(C("a\tb\nc\x01" "d\x7f"), "a b c d "), "Steuerzeichen -> Leerzeichen");
    { char b[64]; for (int i = 0; i < 20000; i++) { for (int k = 0; k < 12; k++) b[k] = (char)(rand() & 0xFF); b[12] = 0; size_t l0 = strlen(b); size_t l = irm_web_utf8_clean(b); if (l != l0 || !utf8_valid(b)) { CHECK(0, "Zufallsbytes ergeben gueltiges UTF-8"); break; } if (i == 19999) CHECK(1, "20000 Zufallsfolgen -> immer gueltiges UTF-8, gleiche Laenge"); } }

    printf("== Query-Parser\n");
    irm_web_dir_req_t q;
    CHECK(irm_web_dir_parse_query(NULL, &q) && q.limit == IRM_WEB_DIR_PAGE_DEFAULT && q.offset == 0 && !q.f.text[0] && q.f.sort == IRM_DIR_SORT_LIST, "NULL/leer -> Vorgaben");
    CHECK(irm_web_dir_parse_query("", &q) && q.limit == 20, "leerer String -> Vorgaben");
    CHECK(irm_web_dir_parse_query("text=Tarlee&country=3&snr=25&free=1&freq=7055000&sort=snr&offset=40&limit=15", &q) && !strcmp(q.f.text, "Tarlee") && q.f.country == 3 && q.f.min_snr == 25 &&
          q.f.only_free && q.f.freq_hz == 7055000 && q.f.sort == IRM_DIR_SORT_SNR && q.offset == 40 && q.limit == 15, "alle Parameter");
    CHECK(irm_web_dir_parse_query("text=M%C3%BCnchen+Ost", &q) && !strcmp(q.f.text, "M\xc3\xbcnchen Ost"), "%%XX und + dekodiert");
    CHECK(irm_web_dir_parse_query("foo=bar&limit=5&x", &q) && q.limit == 5, "unbekannte Schluessel/Reste ignoriert");
    CHECK(irm_web_dir_parse_query("free=0", &q) && !q.f.only_free, "free=0");
    CHECK(!irm_web_dir_parse_query("country=256", &q) && !irm_web_dir_parse_query("snr=100", &q) && !irm_web_dir_parse_query("freq=32000001", &q), "Obergrenzen");
    CHECK(!irm_web_dir_parse_query("limit=0", &q) && !irm_web_dir_parse_query("limit=41", &q) && irm_web_dir_parse_query("limit=40", &q) && q.limit == 40, "limit 1..40");
    CHECK(!irm_web_dir_parse_query("offset=100001", &q) && irm_web_dir_parse_query("offset=100000", &q), "offset <= 100000");
    CHECK(!irm_web_dir_parse_query("country=abc", &q) && !irm_web_dir_parse_query("country=", &q) && !irm_web_dir_parse_query("country=-1", &q) && !irm_web_dir_parse_query("snr=1.5", &q), "keine Zahl");
    CHECK(!irm_web_dir_parse_query("sort=name", &q) && irm_web_dir_parse_query("sort=list", &q), "sort nur list|snr");
    CHECK(!irm_web_dir_parse_query("text=%", &q) && !irm_web_dir_parse_query("text=%4", &q) && !irm_web_dir_parse_query("text=%zz", &q) && irm_web_dir_parse_query("text=%41", &q) && !strcmp(q.f.text, "A"), "kaputtes %%-Muster");
    CHECK(!irm_web_dir_parse_query("text=%00", &q), "NUL per %%00 abgelehnt");
    CHECK(!irm_web_dir_parse_query("text=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &q) && irm_web_dir_parse_query("text=aaaaaaaaaaaaaaaaaaaaaaa", &q), "text <= 23 Bytes");
    CHECK(!irm_web_dir_parse_query("text=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &q), "sehr langer Wert abgelehnt");
    CHECK(!irm_web_dir_parse_query("limit=5", NULL), "out NULL");
    { char big[400]; memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0; irm_web_dir_parse_query(big, &q); CHECK(1, "langer Schluessel ohne '=' ueberlaeuft nichts (ASan)"); }

    printf("== Datenbank + JSON\n");
    const char *path = argc > 1 ? argv[1] : "../irm_dir/test/kiwisdr_sample.js";
    FILE *f = fopen(path, "rb"); assert(f);
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    size_t cap = (size_t)sz + 4096; char *txt = malloc(cap); assert(fread(txt, 1, (size_t)sz, f) == (size_t)sz); txt[sz] = 0; fclose(f);
    /* Zusatzeintrag: erster Block kopiert, Name = 16x "ae" (32 Byte > 31) -> beim Kuerzen wird ein 2-Byte-Zeichen halbiert; Host/Port eindeutig */
    char *b0 = strstr(txt, "\t{"), *b1 = strstr(b0, "\t},\n") + 4;
    size_t blen = (size_t)(b1 - b0);
    char *blk = malloc(blen + 512); memcpy(blk, b0, blen); blk[blen] = 0;
    char *nm = strstr(blk, "\"name\":\""); assert(nm);
    char *nme = strstr(nm + 8, "\",\n");
    memmove(nm + 8 + 32, nme, strlen(nme) + 1);
    for (int i = 0; i < 16; i++) { nm[8 + 2 * i] = (char)0xC3; nm[8 + 2 * i + 1] = (char)0xA4; }
    char *url = strstr(blk, "\"url\":\""); char *urle = strstr(url + 7, "\"\n");
    memmove(url + 7 + 28, urle, strlen(urle) + 1); memcpy(url + 7, "http://utf.test.example:8099", 28);
    char *end = strstr(txt, "]\n;"); assert(end);
    size_t nl = strlen(blk); memmove(end + nl, end, strlen(end) + 1); memcpy(end, blk, nl);
    /* zweiter Zusatzeintrag: Kopie des ersten, aber mit rohem Ungueltig-Byte 0xFF im Namen und anderem Host/Port */
    {
        char *bad = malloc(nl + 128); memcpy(bad, blk, nl + 1);
        assert(replace_in(bad, nl + 128, "utf.test.example:8099", "utf.bad.example:8098"));
        char oldname[40]; for (int i = 0; i < 15; i++) { oldname[2 * i] = (char)0xC3; oldname[2 * i + 1] = (char)0xA4; } oldname[30] = (char)0xC3; oldname[31] = (char)0xA4; oldname[32] = 0;
        assert(replace_in(bad, nl + 128, oldname, "Bad\xff" "Name"));
        end = strstr(txt, "]\n;"); size_t bl = strlen(bad); memmove(end + bl, end, strlen(end) + 1); memcpy(end, bad, bl); free(bad);
    }
    static kiwi_dir_db_t db; static kiwi_dir_update_t up; kiwi_dir_result_t res;
    m_erase(NULL, 0); m_erase(NULL, 1); assert(kiwi_dir_db_open(&db, &g_store, 0) == KIWI_DIR_ERR_EMPTY);
    {
        int rc = kiwi_dir_update_begin(&up, &g_store, &db);
        const size_t tl = strlen(txt);
        for (size_t o = 0; rc == KIWI_DIR_OK && o < tl; o += 777) rc = kiwi_dir_update_push(&up, (uint8_t *)txt + o, tl - o < 777 ? tl - o : 777);
        if (rc == KIWI_DIR_OK) rc = kiwi_dir_update_end(&up, &res);
        if (rc != KIWI_DIR_OK) { printf("  Datenbankaufbau fehlgeschlagen: %s\n", kiwi_dir_strerror(rc)); return 2; }
    }
    printf("  (%u Server in der Test-DB)\n", (unsigned)res.entries);

    irm_web_dir_parse_query("limit=40", &q);
    cJSON *j = irm_web_dir_list_json(&db, &q);
    CHECK(j != NULL, "Liste erzeugt");
    char *s = cJSON_PrintUnformatted(j);
    CHECK(utf8_valid(s), "gesamtes JSON ist gueltiges UTF-8 (%zu Bytes)", strlen(s));
    cJSON *back = cJSON_Parse(s); free(s);
    cJSON *items = cJSON_GetObjectItem(back, "items");
    CHECK(back && cJSON_IsArray(items) && cJSON_GetArraySize(items) > 20 && cJSON_GetArraySize(items) <= 40, "items: %d (<= limit 40)", cJSON_GetArraySize(items));
    CHECK(cJSON_GetObjectItem(back, "total")->valuedouble >= cJSON_GetArraySize(items) && cJSON_GetObjectItem(back, "limit")->valuedouble == 40 && cJSON_GetObjectItem(back, "offset")->valuedouble == 0, "total/limit/offset");
    cJSON *it0 = cJSON_GetArrayItem(items, 0);
    const char *keys[] = { "i", "name", "place", "country", "host", "port", "users", "max", "snr", "lo", "hi" };
    int allk = 1; for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) if (!cJSON_GetObjectItem(it0, keys[k])) allk = 0;
    CHECK(allk && cJSON_IsString(cJSON_GetObjectItem(it0, "host")) && cJSON_IsNumber(cJSON_GetObjectItem(it0, "port")), "Eintrag hat alle Felder mit richtigen Typen");
    CHECK(strlen(cJSON_GetObjectItem(it0, "country")->valuestring) > 0, "Land als Klartext: %s", cJSON_GetObjectItem(it0, "country")->valuestring);
    cJSON_Delete(back); cJSON_Delete(j);

    irm_web_dir_parse_query("text=utf.test", &q);
    j = irm_web_dir_list_json(&db, &q); s = cJSON_PrintUnformatted(j); cJSON_Delete(j);
    back = cJSON_Parse(s); items = cJSON_GetObjectItem(back, "items");
    CHECK(utf8_valid(s) && cJSON_GetArraySize(items) == 1, "Spezialeintrag gefunden, JSON gueltig: %s", s);
    const char *nmv = cJSON_GetObjectItem(cJSON_GetArrayItem(items, 0), "name")->valuestring;
    char want[40]; for (int i = 0; i < 15; i++) { want[2 * i] = (char)0xC3; want[2 * i + 1] = (char)0xA4; } want[30] = 0;
    CHECK(!strcmp(nmv, want), "kiwi_dir kuerzt an UTF-8-Zeichengrenzen: 15x ae, kein halbes Zeichen");
    free(s); cJSON_Delete(back);

    irm_web_dir_parse_query("text=utf.bad", &q);
    j = irm_web_dir_list_json(&db, &q); s = cJSON_PrintUnformatted(j); cJSON_Delete(j);
    back = cJSON_Parse(s); items = cJSON_GetObjectItem(back, "items");
    CHECK(utf8_valid(s) && cJSON_GetArraySize(items) == 1, "Eintrag mit rohem 0xFF im Namen: JSON gueltig");
    nmv = cJSON_GetObjectItem(cJSON_GetArrayItem(items, 0), "name")->valuestring;
    CHECK(!strcmp(nmv, "Bad?Name"), "Ungueltig-Byte -> '?' (%s)", nmv);
    free(s); cJSON_Delete(back);

    irm_web_dir_parse_query("limit=5&offset=100000", &q);
    j = irm_web_dir_list_json(&db, &q); CHECK(j && cJSON_GetArraySize(cJSON_GetObjectItem(j, "items")) == 0, "Offset hinter dem Ende: leere Liste, kein Fehler"); cJSON_Delete(j);
    irm_web_dir_parse_query("sort=snr&limit=3", &q); j = irm_web_dir_list_json(&db, &q);
    { int a = (int)cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(j, "items"), 0), "snr")->valuedouble, b = (int)cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(j, "items"), 1), "snr")->valuedouble;
      CHECK(a >= b, "sort=snr absteigend (%d >= %d)", a, b); } cJSON_Delete(j);
    { irm_web_dir_req_t big = { .limit = 5000 }; j = irm_web_dir_list_json(&db, &big); CHECK(j && cJSON_GetArraySize(cJSON_GetObjectItem(j, "items")) <= IRM_WEB_DIR_PAGE_MAX, "limit wird auch ohne Parser auf %d begrenzt", IRM_WEB_DIR_PAGE_MAX); cJSON_Delete(j); }

    j = irm_web_dir_countries_json(&db); s = cJSON_PrintUnformatted(j);
    CHECK(cJSON_IsArray(j) && cJSON_GetArraySize(j) > 3 && utf8_valid(s) && cJSON_GetObjectItem(cJSON_GetArrayItem(j, 0), "n")->valuedouble >= 1, "Laender: %d Eintraege, gueltiges JSON", cJSON_GetArraySize(j));
    free(s); cJSON_Delete(j);

    printf("== Info-JSON / leere DB\n");
    irm_dir_info_t in = { .available = true, .valid = true, .count = 7, .generation = 3, .state = IRM_DIR_FAILED, .err = KIWI_DIR_ERR_NET, .retry_in_s = 12 };
    j = irm_web_dir_info_json(&in);
    CHECK(!strcmp(cJSON_GetObjectItem(j, "state")->valuestring, "failed") && cJSON_IsTrue(cJSON_GetObjectItem(j, "available")) && cJSON_GetObjectItem(j, "retry_in_s")->valuedouble == 12 && strlen(cJSON_GetObjectItem(j, "msg")->valuestring) > 0, "failed: %s", cJSON_GetObjectItem(j, "msg")->valuestring);
    cJSON_Delete(j);
    in.err = 0; in.state = IRM_DIR_UPDATING; j = irm_web_dir_info_json(&in); CHECK(!strcmp(cJSON_GetObjectItem(j, "state")->valuestring, "updating") && !cJSON_GetObjectItem(j, "msg")->valuestring[0], "updating, msg leer"); cJSON_Delete(j);
    in.state = 99; j = irm_web_dir_info_json(&in); CHECK(!strcmp(cJSON_GetObjectItem(j, "state")->valuestring, "idle"), "unbekannter Zustand -> idle"); cJSON_Delete(j);
    in.err = IRM_DIR_ERR_NOMEM; j = irm_web_dir_info_json(&in); CHECK(!strcmp(cJSON_GetObjectItem(j, "msg")->valuestring, "zu wenig Speicher"), "NOMEM-Text"); cJSON_Delete(j);
    static kiwi_dir_db_t empty; atomic_init(&empty.active, -1);
    irm_web_dir_parse_query(NULL, &q); j = irm_web_dir_list_json(&empty, &q);
    CHECK(j && cJSON_GetObjectItem(j, "total")->valuedouble == 0 && cJSON_GetArraySize(cJSON_GetObjectItem(j, "items")) == 0, "leere DB: total 0, items []"); cJSON_Delete(j);
    j = irm_web_dir_countries_json(&empty); CHECK(j && cJSON_GetArraySize(j) == 0, "leere DB: Laenderliste []"); cJSON_Delete(j);

    free(txt); free(blk);
    printf("== Ergebnis: %s (%d Fehler)\n", fails ? "FEHLER" : "OK", fails);
    return fails ? 1 : 0;
}
