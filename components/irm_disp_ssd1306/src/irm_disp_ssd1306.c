#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "u8g2.h"
#include "irm_board.h"
#include "irm_display.h"
#include "irm_disp_ssd1306.h"

static const char *TAG = "disp_ssd1306";
static u8g2_t s_u8g2;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_buf[160];
static size_t s_len;

/* u8g2-HAL auf Basis des neuen I2C-Master-Treibers (Altcode: Legacy-Treiber driver/i2c.h, in IDF 6 EOL) */
static uint8_t byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr)
{
    switch (msg) {
    case U8X8_MSG_BYTE_INIT: {
        if (s_dev) break;
        const i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                         .device_address = CONFIG_IRM_DISPLAY_I2C_ADDR, .scl_speed_hz = 400000 };
        if (i2c_master_bus_add_device(irm_board_display_i2c(), &dc, &s_dev) != ESP_OK) { ESP_LOGE(TAG, "I2C add_device"); return 0; }
        break;
    }
    case U8X8_MSG_BYTE_SET_DC: break;
    case U8X8_MSG_BYTE_START_TRANSFER: s_len = 0; break;
    case U8X8_MSG_BYTE_SEND:
        if (s_len + arg > sizeof(s_buf)) return 0;
        memcpy(s_buf + s_len, ptr, arg); s_len += arg; break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        if (i2c_master_transmit(s_dev, s_buf, s_len, 100) != ESP_OK) return 0;
        break;
    default: return 0;
    }
    (void)u8x8;
    return 1;
}

static uint8_t gpio_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg, void *ptr)
{
    (void)u8x8; (void)ptr;
    switch (msg) {
    case U8X8_MSG_DELAY_MILLI: vTaskDelay(pdMS_TO_TICKS(arg ? arg : 1)); break;
    case U8X8_MSG_DELAY_10MICRO: esp_rom_delay_us(10u * arg); break;
    case U8X8_MSG_DELAY_100NANO: esp_rom_delay_us(1); break;
    default: break;
    }
    return 1;
}

static esp_err_t init(void)
{
    i2c_master_bus_handle_t bus = irm_board_display_i2c();
    if (!bus) return ESP_ERR_INVALID_STATE;
    if (i2c_master_probe(bus, CONFIG_IRM_DISPLAY_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGE(TAG, "Kein I2C-Geraet auf Adresse 0x%02X (%s): Adresse (0x3C/0x3D), Verdrahtung, Pull-ups und Versorgung pruefen",
                 CONFIG_IRM_DISPLAY_I2C_ADDR, irm_board_display_i2c_desc());
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "SSD1306 gefunden auf 0x%02X", CONFIG_IRM_DISPLAY_I2C_ADDR);
#if CONFIG_IRM_DISPLAY_SSD1306_HEIGHT == 32
    u8g2_Setup_ssd1306_i2c_128x32_univision_f(&s_u8g2, U8G2_R0, byte_cb, gpio_delay_cb);
#else
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&s_u8g2, U8G2_R0, byte_cb, gpio_delay_cb);
#endif
    u8x8_SetI2CAddress(&s_u8g2.u8x8, CONFIG_IRM_DISPLAY_I2C_ADDR << 1);
    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    return s_dev ? ESP_OK : ESP_FAIL;
}

static void render(const irm_status_t *st)
{
    char t[64], i[40], v[16];
    irm_view_title(st, t, sizeof(t));
    irm_view_info(st, i, sizeof(i));
    irm_view_volume(st, v, sizeof(v));
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SetFont(&s_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&s_u8g2, 0, 10, i);
    u8g2_DrawStr(&s_u8g2, 128 - u8g2_GetStrWidth(&s_u8g2, v), 10, v);
#if CONFIG_IRM_DISPLAY_SSD1306_HEIGHT == 64
    u8g2_SetFont(&s_u8g2, u8g2_font_helvB10_tf);
    u8g2_DrawStr(&s_u8g2, 0, 34, t);               /* lange Namen werden am Rand abgeschnitten */
    u8g2_SetFont(&s_u8g2, u8g2_font_5x7_tf);
    if (st->ip[0]) u8g2_DrawStr(&s_u8g2, 0, 62, st->ip);
    u8g2_DrawFrame(&s_u8g2, 0, 48, 128, 6);
    u8g2_DrawBox(&s_u8g2, 0, 48, (128 * st->volume) / 100, 6);
#else
    u8g2_DrawStr(&s_u8g2, 0, 28, t);
#endif
    u8g2_SendBuffer(&s_u8g2);
}

static void deinit(void) { u8g2_SetPowerSave(&s_u8g2, 1); }

const irm_display_backend_t irm_disp_ssd1306_backend = { "ssd1306", init, render, deinit };
