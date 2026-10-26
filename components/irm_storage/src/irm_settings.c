#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "irm_settings.h"

#define NS "irm"
static const char *TAG = "irm_settings";

esp_err_t irm_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t irm_settings_load(irm_settings_t *o)
{
    memset(o, 0, sizeof(*o));
    o->volume = 30; o->station = 0;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e != ESP_OK) return ESP_OK;  /* erster Start: Defaults */
    uint8_t v;
    if (nvs_get_u8(h, "vol", &v) == ESP_OK && v <= 100) o->volume = v;
    nvs_get_i32(h, "stn", &o->station);
    size_t sz = sizeof(o->eq);
    if (nvs_get_blob(h, "eq", &o->eq, &sz) == ESP_OK && sz == sizeof(o->eq)) o->has_eq = true;
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t with_nvs(esp_err_t (*fn)(nvs_handle_t, const void *), const void *arg)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = fn(h, arg);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGW(TAG, "NVS-Schreiben fehlgeschlagen: %s", esp_err_to_name(e));
    return e;
}
static esp_err_t f_vol(nvs_handle_t h, const void *a) { return nvs_set_u8(h, "vol", *(const uint8_t *)a); }
static esp_err_t f_stn(nvs_handle_t h, const void *a) { return nvs_set_i32(h, "stn", *(const int32_t *)a); }
static esp_err_t f_eq(nvs_handle_t h, const void *a) { return nvs_set_blob(h, "eq", a, sizeof(irm_eq_t)); }

esp_err_t irm_settings_save_volume(uint8_t v) { return with_nvs(f_vol, &v); }
esp_err_t irm_settings_save_station(int32_t i) { return with_nvs(f_stn, &i); }
esp_err_t irm_settings_save_eq(const irm_eq_t *eq) { return with_nvs(f_eq, eq); }

esp_err_t irm_settings_load_wifi(char *ssid, size_t ssid_sz, char *psk, size_t psk_sz)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e != ESP_OK) return e;
    e = nvs_get_str(h, "ssid", ssid, &ssid_sz);
    if (e == ESP_OK) {
        e = nvs_get_str(h, "psk", psk, &psk_sz);
        if (e == ESP_ERR_NVS_NOT_FOUND) { psk[0] = 0; e = ESP_OK; }
    }
    nvs_close(h);
    return e;
}

esp_err_t irm_settings_save_wifi(const char *ssid, const char *psk)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_str(h, "ssid", ssid);
    if (e == ESP_OK) e = nvs_set_str(h, "psk", psk ? psk : "");
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
