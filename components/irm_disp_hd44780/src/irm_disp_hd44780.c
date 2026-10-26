/* HD44780 im 4-Bit-Modus ueber PCF8574-Rucksack. Pinbelegung des gaengigen Adapters:
 * P0=RS P1=RW P2=E P3=Backlight P4..P7=D4..D7 */
#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "irm_board.h"
#include "irm_display.h"
#include "irm_disp_hd44780.h"

#define COLS CONFIG_IRM_DISPLAY_LCD_COLS
#define ROWS CONFIG_IRM_DISPLAY_LCD_ROWS
#define RS 0x01
#define EN 0x04
#define BL 0x08

static const char *TAG = "disp_hd44780";
static i2c_master_dev_handle_t s_dev;
static char s_shadow[ROWS][COLS + 1];

static esp_err_t pcf(uint8_t v) { return i2c_master_transmit(s_dev, &v, 1, 50); }

static void nibble(uint8_t n, uint8_t rs)
{
    uint8_t d = (uint8_t)((n << 4) | BL | rs);
    pcf(d | EN);
    esp_rom_delay_us(2);
    pcf(d & (uint8_t)~EN);
    esp_rom_delay_us(60);
}
static void byte(uint8_t b, uint8_t rs) { nibble(b >> 4, rs); nibble(b & 0x0F, rs); }
static void cmd(uint8_t c) { byte(c, 0); }

static esp_err_t init(void)
{
    const i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                     .device_address = CONFIG_IRM_DISPLAY_I2C_ADDR, .scl_speed_hz = 100000 };
    i2c_master_bus_handle_t bus = irm_board_display_i2c();
    if (!bus) return ESP_ERR_INVALID_STATE;
    if (i2c_master_probe(bus, CONFIG_IRM_DISPLAY_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGE(TAG, "Kein I2C-Geraet auf Adresse 0x%02X (%s): Adresse (0x27/0x3F), Verdrahtung, Pull-ups und Versorgung pruefen",
                 CONFIG_IRM_DISPLAY_I2C_ADDR, irm_board_display_i2c_desc());
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "HD44780-Adapter gefunden auf 0x%02X", CONFIG_IRM_DISPLAY_I2C_ADDR);
    esp_err_t e = i2c_master_bus_add_device(bus, &dc, &s_dev);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(50));
    nibble(0x3, 0); vTaskDelay(pdMS_TO_TICKS(5));
    nibble(0x3, 0); esp_rom_delay_us(150);
    nibble(0x3, 0); esp_rom_delay_us(150);
    nibble(0x2, 0);                       /* 4-Bit-Modus */
    cmd(ROWS > 1 ? 0x28 : 0x20);          /* Function set: 4 Bit, 5x8 */
    cmd(0x0C);                            /* Display an, Cursor aus */
    cmd(0x01); vTaskDelay(pdMS_TO_TICKS(3));
    cmd(0x06);                            /* Entry mode: Cursor +1 */
    memset(s_shadow, 0, sizeof(s_shadow));
    return ESP_OK;
}

static void put_line(int row, const char *txt)
{
    char line[COLS + 1];
    snprintf(line, sizeof(line), "%-*.*s", COLS, COLS, txt);   /* auf Spaltenbreite auffuellen/kuerzen */
    if (strcmp(line, s_shadow[row]) == 0) return;               /* nur geaenderte Zeilen schreiben */
    static const uint8_t base[4] = { 0x00, 0x40, 0x14, 0x54 };
    cmd((uint8_t)(0x80 | (base[row] )));
    for (int i = 0; i < COLS; i++) byte((uint8_t)line[i], RS);
    memcpy(s_shadow[row], line, sizeof(line));
}

static void render(const irm_status_t *st)
{
    char a[64], b[48], c[24];
    irm_view_title(st, a, sizeof(a));
    irm_view_info(st, b, sizeof(b));
    irm_view_volume(st, c, sizeof(c));
    put_line(0, a);
    if (ROWS >= 2) { char l[80]; snprintf(l, sizeof(l), "%s %s", b, c); put_line(1, l); }
    if (ROWS >= 3) put_line(2, st->ip);
    if (ROWS >= 4) put_line(3, st->source == IRM_SOURCE_RADIO ? "Internetradio" : "Mediaplayer");
}

static void deinit(void) { if (s_dev) { cmd(0x08); pcf(0x00); } }

const irm_display_backend_t irm_disp_hd44780_backend = { "hd44780", init, render, deinit };
