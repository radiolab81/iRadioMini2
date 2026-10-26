#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Initialisiert netif/event-loop/Wi-Fi und verbindet sich (nicht blockierend, Reconnect mit Backoff).
 *  Der Verbindungszustand erscheint in irm_status_t (net_up, ip). ssid==NULL -> Kconfig-Fallback. */
esp_err_t irm_net_start(const char *ssid, const char *psk);
#ifdef __cplusplus
}
#endif
