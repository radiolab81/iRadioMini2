/* Host-Test des ECHTEN irm_dir.c (Update-Task ueber POSIX-Shim) gegen einen lokalen HTTP-Server; Store im RAM. Treiber: e2e_irm_dir.py */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "irm_dir.h"
#include "irm_dir_store.h"

static int fails;
#define CHECK(c, ...) do { int ok_ = !!(c); printf("  %s ", ok_ ? "ok  " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

#define SLOT (192u * 1024u)
static uint8_t g_mem[KIWI_DIR_SLOTS][SLOT];
static int m_read(void *c, int s, size_t o, void *b, size_t n) { (void)c; if (o + n > SLOT) return -1; memcpy(b, g_mem[s] + o, n); return 0; }
static int m_erase(void *c, int s) { (void)c; memset(g_mem[s], 0xFF, SLOT); return 0; }
static int m_write(void *c, int s, size_t o, const void *b, size_t n) { (void)c; if (o + n > SLOT) return -1; memcpy(g_mem[s] + o, b, n); return 0; }
static kiwi_dir_store_t g_store = { SLOT, 4096, m_read, m_erase, m_write, NULL };
static int g_have_store = 1;
const kiwi_dir_store_t *irm_dir_store_get(void) { return g_have_store ? &g_store : NULL; }

static int wait_state(int not_state, double secs)
{
    irm_dir_info_t i;
    for (double t = 0; t < secs; t += 0.05) { irm_dir_info(&i); if (i.state != not_state) return 1; usleep(50000); }
    return 0;
}

int main(void)
{
    irm_dir_info_t i;
    printf("== ohne Partition\n");
    g_have_store = 0;
    CHECK(irm_dir_init() == ESP_OK, "init ohne Partition ist kein Fehler");
    irm_dir_info(&i);
    CHECK(!i.available && !i.valid && i.count == 0 && i.state == IRM_DIR_IDLE, "available=false, leer");
    CHECK(irm_dir_update(true) == ESP_ERR_NOT_SUPPORTED, "Update nicht moeglich");
    CHECK(irm_dir_search(irm_dir_db(), &(irm_dir_filter_t){ 0 }, 0, 0, NULL, NULL) == 0, "Suche in leerer DB harmlos");

    printf("== Start mit leerem Flash\n");
    g_have_store = 1; memset(g_mem, 0xFF, sizeof g_mem);
    CHECK(irm_dir_init() == ESP_OK, "init");
    irm_dir_info(&i);
    CHECK(i.available && !i.valid && i.count == 0 && i.retry_in_s == 0, "verfuegbar, noch keine Datenbank");

    printf("== Update 1 (Server antwortet langsam)\n");
    CHECK(irm_dir_update(false) == ESP_OK, "Update gestartet");
    irm_dir_info(&i);
    CHECK(i.state == IRM_DIR_UPDATING && i.retry_in_s > 0, "Zustand UPDATING, Mindestabstand laeuft (%us)", (unsigned)i.retry_in_s);
    CHECK(irm_dir_update(true) == ESP_ERR_INVALID_STATE, "zweites Update waehrend des ersten: abgelehnt (auch mit force)");
    CHECK(wait_state(IRM_DIR_UPDATING, 20), "Update 1 beendet");
    irm_dir_info(&i);
    CHECK(i.state == IRM_DIR_OK && i.err == 0 && i.valid && i.count >= 30 && i.generation == 1, "OK: %u Server, Generation %u, %u Bytes geladen", (unsigned)i.count, (unsigned)i.generation, (unsigned)i.bytes);
    uint32_t count1 = i.count;
    kiwi_dir_rec_t r;
    CHECK(irm_dir_get(0, &r) && r.host[0] && r.port, "Eintrag 0 lesbar: %s:%u", r.host, (unsigned)r.port);
    CHECK(!irm_dir_get(count1, &r) && !irm_dir_get(0xFFFFFFFFu, &r), "Index ausserhalb der Datenbank abgelehnt");

    printf("== Mindestabstand\n");
    CHECK(irm_dir_update(false) == ESP_ERR_TIMEOUT, "zu frueh ohne force: ESP_ERR_TIMEOUT");
    irm_dir_info(&i);
    CHECK(i.state == IRM_DIR_OK && i.retry_in_s > 0, "Zustand bleibt OK, retry_in_s = %u", (unsigned)i.retry_in_s);

    printf("== Update 2: Server liefert Fehler -> Datenbank bleibt\n");
    CHECK(irm_dir_update(true) == ESP_OK, "mit force gestartet");
    CHECK(wait_state(IRM_DIR_UPDATING, 20), "beendet");
    irm_dir_info(&i);
    CHECK(i.state == IRM_DIR_FAILED && i.err != 0, "FAILED: %s", irm_dir_err_text(i.err));
    CHECK(i.valid && i.count == count1 && i.generation == 1, "Datenbank unveraendert (%u Server, Generation %u)", (unsigned)i.count, (unsigned)i.generation);

    printf("== Update 3: wieder ok -> Generation steigt\n");
    sleep(2);   /* Mindestabstand laeuft ab, kein force noetig */
    CHECK(irm_dir_update(false) == ESP_OK, "nach Ablauf des Mindestabstands ohne force");
    CHECK(wait_state(IRM_DIR_UPDATING, 20), "beendet");
    irm_dir_info(&i);
    CHECK(i.state == IRM_DIR_OK && i.generation == 2 && i.count == count1, "Generation 2, %u Server", (unsigned)i.count);

    printf("== Fehlertexte\n");
    CHECK(!strcmp(irm_dir_err_text(IRM_DIR_ERR_NOMEM), "zu wenig Speicher") && irm_dir_err_text(KIWI_DIR_ERR_NET)[0], "Texte vorhanden");

    printf("== Ergebnis: %s (%d Fehler)\n", fails ? "FEHLER" : "OK", fails);
    return fails ? 1 : 0;
}
