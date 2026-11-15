/* Host-Test: gcc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -Iinclude src/irm_netin_policy.c test/host/test_netin_policy.c -o /tmp/tnp && /tmp/tnp */
#include <stdio.h>
#include <string.h>
#include "irm_netin_policy.h"

static int fails;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); else { printf("  FEHLER %s\n", m); fails++; } } while (0)
#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((b) << 16) | ((c) << 8) | (d))

int main(void)
{
    const uint32_t local = IP(192, 168, 1, 50), m24 = IP(255, 255, 255, 0);
    printf("== Subnetz\n");
    CHECK(irm_netin_peer_allowed(IP(192, 168, 1, 7), local, m24), "Geraet im Subnetz erlaubt");
    CHECK(irm_netin_peer_allowed(IP(192, 168, 1, 254), local, m24) && irm_netin_peer_allowed(IP(192, 168, 1, 1), local, m24), ".1 und .254 erlaubt");
    CHECK(!irm_netin_peer_allowed(IP(192, 168, 2, 7), local, m24), "anderes /24 abgelehnt");
    CHECK(!irm_netin_peer_allowed(IP(10, 0, 0, 7), local, m24) && !irm_netin_peer_allowed(IP(8, 8, 8, 8), local, m24), "fremde Netze/Internet abgelehnt");
    CHECK(!irm_netin_peer_allowed(IP(192, 168, 1, 0), local, m24) && !irm_netin_peer_allowed(IP(192, 168, 1, 255), local, m24), "Netz- und Broadcastadresse abgelehnt");
    CHECK(!irm_netin_peer_allowed(0, local, m24) && !irm_netin_peer_allowed(0xFFFFFFFFu, local, m24) && !irm_netin_peer_allowed(IP(224, 0, 0, 251), local, m24), "0.0.0.0, 255.255.255.255, Multicast abgelehnt");
    CHECK(irm_netin_peer_allowed(IP(172, 16, 5, 9), IP(172, 16, 4, 3), IP(255, 255, 254, 0)), "/23: Nachbar-/24 im selben /23 erlaubt");
    CHECK(!irm_netin_peer_allowed(IP(172, 16, 6, 9), IP(172, 16, 4, 3), IP(255, 255, 254, 0)), "/23: ausserhalb abgelehnt");
    CHECK(!irm_netin_peer_allowed(IP(192, 168, 1, 7), 0, m24) && !irm_netin_peer_allowed(IP(192, 168, 1, 7), local, 0), "ohne Netzwerk (IP/Maske 0) alles abgelehnt");
    CHECK(!irm_netin_peer_allowed(IP(192, 168, 1, 7), local, 0xFFFFFFFFu) && !irm_netin_peer_allowed(IP(192, 168, 1, 7), local, 0xFFFFFFFEu), "/32 und /31 abgelehnt");
    CHECK(irm_netin_peer_allowed(IP(10, 1, 1, 1), IP(10, 1, 1, 2), 0xFFFFFFFCu), "/30: der andere Host erlaubt");

    printf("== Absender-Miete (immer nur einer)\n");
    irm_netin_lease_t l; irm_netin_lease_release(&l);
    const uint32_t a = IP(192, 168, 1, 7), b = IP(192, 168, 1, 8);
    CHECK(irm_netin_lease_admit(&l, a, 1000, 2000) == IRM_NETIN_ADMIT_NEW, "erster Absender: NEW");
    CHECK(irm_netin_lease_admit(&l, a, 1500, 2000) == IRM_NETIN_ADMIT_SAME, "derselbe: SAME");
    CHECK(irm_netin_lease_admit(&l, b, 1600, 2000) == IRM_NETIN_DENY, "zweiter Absender waehrend der Miete: DENY");
    CHECK(irm_netin_lease_admit(&l, b, 3400, 2000) == IRM_NETIN_DENY, "knapp vor Ablauf (1900 ms nach letztem Paket): weiter DENY");
    CHECK(!irm_netin_lease_expired(&l, 3500, 2000) && irm_netin_lease_expired(&l, 3501, 2000), "Ablauf genau nach timeout (>2000 ms)");
    CHECK(irm_netin_lease_admit(&l, b, 3600, 2000) == IRM_NETIN_ADMIT_NEW && l.ip == b, "nach Ablauf uebernimmt der zweite: NEW");
    CHECK(irm_netin_lease_admit(&l, a, 3700, 2000) == IRM_NETIN_DENY, "der erste ist jetzt ausgesperrt");
    CHECK(irm_netin_lease_admit(&l, b, 6000, 2000) == IRM_NETIN_ADMIT_NEW, "derselbe Absender nach Pause: NEW (Strom neu gestartet -> Puffer leeren)");
    irm_netin_lease_release(&l);
    CHECK(!l.active && !irm_netin_lease_expired(&l, 99999, 2000), "release: frei, nie abgelaufen");
    CHECK(irm_netin_lease_admit(&l, a, 10, 2000) == IRM_NETIN_ADMIT_NEW, "nach release sofort wieder vergebbar");

    char s[20];
    irm_netin_ip_str(IP(192, 168, 1, 7), s, sizeof s);
    CHECK(!strcmp(s, "192.168.1.7"), "ip_str");
    irm_netin_ip_str(IP(255, 255, 255, 255), s, 16);
    CHECK(!strcmp(s, "255.255.255.255"), "ip_str passt in 16 Byte");
    printf("== Ergebnis: %s\n", fails ? "FEHLER" : "OK");
    return fails ? 1 : 0;
}
