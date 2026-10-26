/* iRadioMini2 - Startsequenz. Gesamte Logik liegt in den irm_* Komponenten; hier wird nur verdrahtet. */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "irm_board.h"
#include "irm_control.h"
#include "irm_core.h"
#include "irm_display.h"
#include "irm_files.h"
#include "irm_input.h"
#include "irm_net.h"
#include "irm_settings.h"
#include "irm_tx.h"
#include "irm_web.h"
#include "irm_wifi_conf.h"
#if CONFIG_IRM_DISPLAY_SSD1306
#include "irm_disp_ssd1306.h"
#elif CONFIG_IRM_DISPLAY_HD44780
#include "irm_disp_hd44780.h"
#elif CONFIG_IRM_DISPLAY_SERVO
#include "irm_disp_servo.h"
#elif CONFIG_IRM_DISPLAY_LVGL
#include "irm_disp_lvgl.h"
#endif

static const char *TAG = "irm_main";

/* Zugangsdaten: wifi.txt auf SD (wird in NVS uebernommen) > NVS > Kconfig. Das Passwort wird nie geloggt. */
static void resolve_wifi(char *ssid, size_t ssid_sz, char *psk, size_t psk_sz)
{
    ssid[0] = psk[0] = 0;
    if (irm_board_sd_mounted()) {
        char *txt; size_t len;
        if (irm_files_read(IRM_SD_MOUNT_POINT "/wifi.txt", 2048, &txt, &len) == ESP_OK) {
            bool ok = irm_wifi_conf_parse(txt, len, ssid, ssid_sz, psk, psk_sz);
            memset(txt, 0, len); free(txt);
            if (ok) {
                ESP_LOGI(TAG, "WLAN-Zugangsdaten von SD (SSID '%s') -> NVS", ssid);
                irm_settings_save_wifi(ssid, psk);
                return;
            }
            ESP_LOGW(TAG, "wifi.txt ohne gueltigen network-Block");
        }
    }
    if (irm_settings_load_wifi(ssid, ssid_sz, psk, psk_sz) == ESP_OK && ssid[0]) return;
    ssid[0] = psk[0] = 0;   /* irm_net faellt auf Kconfig zurueck */
}

void app_main(void)
{
    ESP_ERROR_CHECK(irm_settings_init());
    ESP_ERROR_CHECK(irm_core_init());
    ESP_ERROR_CHECK(irm_board_init());
    if (irm_board_mount_sd() != ESP_OK) ESP_LOGW(TAG, "laeuft ohne SD-Karte (kein playlist.m3u / Mediaplayer)");

    char ssid[33], psk[65];
    resolve_wifi(ssid, sizeof(ssid), psk, sizeof(psk));

    ESP_ERROR_CHECK(irm_control_start());
    if (irm_net_start(ssid, psk) != ESP_OK) ESP_LOGE(TAG, "WLAN nicht gestartet - Radio bleibt ohne Netz");
    memset(psk, 0, sizeof(psk));

    ESP_ERROR_CHECK(irm_input_start());
#if CONFIG_IRM_DISPLAY_SSD1306
    irm_display_start(&irm_disp_ssd1306_backend);
#elif CONFIG_IRM_DISPLAY_HD44780
    irm_display_start(&irm_disp_hd44780_backend);
#elif CONFIG_IRM_DISPLAY_SERVO
    irm_display_start(&irm_disp_servo_backend);
#elif CONFIG_IRM_DISPLAY_LVGL
    irm_display_start(&irm_disp_lvgl_backend);
#endif
    if (irm_tx_start() == ESP_OK) ESP_LOGI(TAG, "AM-Sender aktiv");   /* ESP_ERR_NOT_SUPPORTED wenn abgeschaltet */
    ESP_ERROR_CHECK(irm_web_start());
    ESP_LOGI(TAG, "iRadioMini2 gestartet");
}
