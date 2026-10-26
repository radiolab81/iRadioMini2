/* LVGL-Backend: SPI-Farbdisplay ueber esp_lcd + esp_lvgl_port (Tick, Task, Locking, Puffer, Flush).
 * Ersetzt lvgl_esp32_drivers + handgeschriebene lvgl_tick-Task des Altprojekts. */
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"
#if CONFIG_IRM_LCD_ILI9341
#include "esp_lcd_ili9341.h"
#else
#include "esp_lcd_panel_st7789.h"
#endif
#include "irm_disp_lvgl.h"
#include "irm_skin.h"

/* Bool-Optionen mit Wert "n" fehlen in sdkconfig.h -> explizit auf true/false abbilden. */
#ifdef CONFIG_IRM_LCD_SWAP_XY
#define LCD_SWAP_XY true
#else
#define LCD_SWAP_XY false
#endif
#ifdef CONFIG_IRM_LCD_MIRROR_X
#define LCD_MIRROR_X true
#else
#define LCD_MIRROR_X false
#endif
#ifdef CONFIG_IRM_LCD_MIRROR_Y
#define LCD_MIRROR_Y true
#else
#define LCD_MIRROR_Y false
#endif
#ifdef CONFIG_IRM_LCD_INVERT
#define LCD_INVERT true
#else
#define LCD_INVERT false
#endif
#ifdef CONFIG_IRM_LCD_BGR
#define LCD_RGB_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#else
#define LCD_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#endif

static const char *TAG = "disp_lvgl";
static lv_display_t *s_disp;

static esp_err_t init(void)
{
    const spi_host_device_t host = CONFIG_IRM_LCD_SPI_HOST == 1 ? SPI2_HOST : SPI3_HOST;
    const spi_bus_config_t bus = {
        .sclk_io_num = CONFIG_IRM_LCD_SCLK, .mosi_io_num = CONFIG_IRM_LCD_MOSI,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_IRM_LCD_H_RES * CONFIG_IRM_LCD_BUF_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_IRM_LCD_DC, .cs_gpio_num = CONFIG_IRM_LCD_CS,
        .pclk_hz = CONFIG_IRM_LCD_SPI_MHZ * 1000 * 1000, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .spi_mode = 0, .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)host, &io_cfg, &io), TAG, "panel io");

    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = CONFIG_IRM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ORDER,
        .bits_per_pixel = 16,
    };
#if CONFIG_IRM_LCD_ILI9341
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &pc, &panel), TAG, "ili9341");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &pc, &panel), TAG, "st7789");
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, LCD_INVERT));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

#if CONFIG_IRM_LCD_BL >= 0
    const gpio_config_t bl = { .pin_bit_mask = 1ULL << CONFIG_IRM_LCD_BL, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(CONFIG_IRM_LCD_BL, 1);
#endif

    const lvgl_port_cfg_t lp = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lp), TAG, "lvgl port");
    const lvgl_port_display_cfg_t dc = {
        .io_handle = io, .panel_handle = panel,
        .buffer_size = CONFIG_IRM_LCD_H_RES * CONFIG_IRM_LCD_BUF_LINES,
        .double_buffer = true,
        .hres = CONFIG_IRM_LCD_H_RES, .vres = CONFIG_IRM_LCD_V_RES,
        .monochrome = false,
        .rotation = { .swap_xy = LCD_SWAP_XY, .mirror_x = LCD_MIRROR_X, .mirror_y = LCD_MIRROR_Y },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = { .buff_dma = true, .swap_bytes = true },   /* SPI sendet High-Byte zuerst */
    };
    s_disp = lvgl_port_add_disp(&dc);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "lvgl_port_add_disp");

    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    irm_skin_active.create(scr);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Skin: %s", irm_skin_active.name);
    return ESP_OK;
}

static void render(const irm_status_t *st)
{
    if (lvgl_port_lock(200)) {
        irm_skin_active.update(st);
        lvgl_port_unlock();
    }
}

static void deinit(void)
{
    if (s_disp) lvgl_port_remove_disp(s_disp);
    s_disp = NULL;
}

const irm_display_backend_t irm_disp_lvgl_backend = { "lvgl", init, render, deinit };
