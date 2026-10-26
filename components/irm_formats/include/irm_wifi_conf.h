#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/**
 * wpa_supplicant-aehnliche wifi.txt parsen (erster network={}-Block mit ssid).
 * psk fehlt oder key_mgmt=NONE -> psk wird leer (offenes Netz).
 * @return true wenn eine SSID gefunden wurde. Ausgaben sind immer 0-terminiert.
 */
bool irm_wifi_conf_parse(const char *text, size_t len,
                         char *ssid, size_t ssid_sz, char *psk, size_t psk_sz);
#ifdef __cplusplus
}
#endif
