#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "irm_core.h"
#include "irm_net.h"

static const char *TAG = "irm_net";
static esp_netif_t *s_netif;
static esp_timer_handle_t s_retry_timer;
static int s_backoff_s = 1;

static void m_up(irm_status_t *s, void *c) { const char *ip = c; s->net_up = ip != NULL; snprintf(s->ip, sizeof(s->ip), "%s", ip ? ip : ""); }

static void retry_cb(void *arg) { (void)arg; esp_wifi_connect(); }

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "getrennt (Grund %d), neuer Versuch in %d s", d->reason, s_backoff_s);
        irm_status_update(m_up, NULL);
        irm_cmd_post_id(IRM_CMD_INTERNAL_NET_CHANGED, 0);
        esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_s * 1000000ULL);
        int max = CONFIG_IRM_WIFI_MAX_BACKOFF_S;
        s_backoff_s = (s_backoff_s * 2 > max) ? max : s_backoff_s * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "verbunden, IP %s", ip);
        s_backoff_s = 1;
        irm_status_update(m_up, ip);
        irm_cmd_post_id(IRM_CMD_INTERNAL_NET_CHANGED, 1);
    }
}

esp_err_t irm_net_start(const char *ssid, const char *psk)
{
    if (!ssid || !ssid[0]) { ssid = CONFIG_IRM_WIFI_SSID; psk = CONFIG_IRM_WIFI_PASSWORD; }
    if (!ssid[0]) { ESP_LOGE(TAG, "keine WLAN-Zugangsdaten (wifi.txt / NVS / Kconfig)"); return ESP_ERR_NOT_FOUND; }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, CONFIG_IRM_WIFI_HOSTNAME);

    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_retry_timer));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, psk ? psk : "", sizeof(wc.sta.password));
    wc.sta.threshold.authmode = (psk && psk[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));  /* Streaming: Latenz/Aussetzer vermeiden */
    return esp_wifi_start();
}
