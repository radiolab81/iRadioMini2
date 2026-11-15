/* Zulassungsregeln fuer den Netzwerk-Audioeingang, rein C (host-testbar, test/host/test_netin_policy.c). Alle IPv4-Adressen in Host-Byte-Order. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** true, wenn peer ein gueltiger Absender im Subnetz (local/mask) ist: nicht 0, nicht Netz-/Broadcast-Adresse, kein Multicast, Maske mind. /30. */
bool irm_netin_peer_allowed(uint32_t peer, uint32_t local, uint32_t mask);

/** Es darf immer nur ein Absender senden: wer zuerst Daten schickt, haelt die "Miete", bis er timeout_ms lang nichts mehr geschickt hat. */
typedef struct { bool active; uint32_t ip; int64_t last_ms; } irm_netin_lease_t;
typedef enum { IRM_NETIN_DENY = 0, IRM_NETIN_ADMIT_NEW, IRM_NETIN_ADMIT_SAME } irm_netin_admit_t;

/** NEW: Absender wurde (neu) zugelassen (Puffer leeren!); SAME: aktueller Absender (Frist verlaengert); DENY: anderer Absender haelt die Miete. */
irm_netin_admit_t irm_netin_lease_admit(irm_netin_lease_t *l, uint32_t ip, int64_t now_ms, int64_t timeout_ms);
bool irm_netin_lease_expired(const irm_netin_lease_t *l, int64_t now_ms, int64_t timeout_ms);
void irm_netin_lease_release(irm_netin_lease_t *l);

/** "a.b.c.d" (n >= 16) */
void irm_netin_ip_str(uint32_t ip, char *out, size_t n);

#ifdef __cplusplus
}
#endif
