#include "irm_netin_policy.h"
#include <stdio.h>
#include <string.h>

bool irm_netin_peer_allowed(uint32_t peer, uint32_t local, uint32_t mask)
{
    if (mask == 0 || mask > 0xFFFFFFFCu || peer == 0 || local == 0) return false;     /* kein Netz bekannt / kleiner als /30 */
    if (peer >= 0xE0000000u) return false;                                               /* Multicast, Klasse E, Broadcast */
    if ((peer & mask) != (local & mask)) return false;                                   /* anderes Subnetz */
    if ((peer & ~mask) == 0 || (peer & ~mask) == ~mask) return false;                    /* Netz- bzw. Broadcast-Adresse */
    return true;
}

bool irm_netin_lease_expired(const irm_netin_lease_t *l, int64_t now_ms, int64_t timeout_ms)
{
    return l->active && now_ms - l->last_ms > timeout_ms;
}

irm_netin_admit_t irm_netin_lease_admit(irm_netin_lease_t *l, uint32_t ip, int64_t now_ms, int64_t timeout_ms)
{
    if (!l->active || irm_netin_lease_expired(l, now_ms, timeout_ms)) {
        l->active = true; l->ip = ip; l->last_ms = now_ms;
        return IRM_NETIN_ADMIT_NEW;
    }
    if (l->ip == ip) { l->last_ms = now_ms; return IRM_NETIN_ADMIT_SAME; }
    return IRM_NETIN_DENY;
}

void irm_netin_lease_release(irm_netin_lease_t *l) { memset(l, 0, sizeof(*l)); }

void irm_netin_ip_str(uint32_t ip, char *out, size_t n)
{
    snprintf(out, n, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 255), (unsigned)((ip >> 8) & 255), (unsigned)(ip & 255));
}
