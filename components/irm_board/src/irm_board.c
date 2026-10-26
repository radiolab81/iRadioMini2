#include "sdkconfig.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "irm_board.h"
#if CONFIG_IRM_CODEC_ES8388
#include "es8388_codec.h"
#elif CONFIG_IRM_CODEC_ES8311
#include "es8311_codec.h"
#else
#include "dummy_codec.h"
#endif

#define STR__(x) #x
#define STR_(x) STR__(x)
static const char *TAG = "irm_board";
static i2c_master_bus_handle_t s_i2c;       /* Codec-Bus */
static i2c_master_bus_handle_t s_disp_i2c;  /* Display-Bus */
#if CONFIG_IRM_OUT_CODEC
static i2s_chan_handle_t s_tx;
#endif
static esp_codec_dev_handle_t s_codec;
static sdmmc_card_t *s_card;

i2c_master_bus_handle_t irm_board_i2c(void) { return s_i2c; }
i2c_master_bus_handle_t irm_board_display_i2c(void) { return s_disp_i2c; }
const char *irm_board_display_i2c_desc(void)
{
#ifdef CONFIG_IRM_DISP_I2C_SHARED
    return "Codec-Bus (geteilt)";
#elif defined(CONFIG_IRM_DISP_I2C_SDA)
    return "Controller " STR_(CONFIG_IRM_DISP_I2C_PORT) ", SDA=GPIO" STR_(CONFIG_IRM_DISP_I2C_SDA) ", SCL=GPIO" STR_(CONFIG_IRM_DISP_I2C_SCL);
#else
    return "kein I2C-Display konfiguriert";
#endif
}
esp_codec_dev_handle_t irm_board_codec(void) { return s_codec; }
bool irm_board_sd_mounted(void) { return s_card != NULL; }


#if CONFIG_IRM_OUT_CODEC && CONFIG_IRM_HP_DETECT_GPIO >= 0
/* Kopfhoerer-Erkennung (AudioKit: GPIO39). Entprellt (3 x 100 ms), protokolliert jeden Wechsel; optional PA abschalten. */
/* Teilt sich der Display-I2C-Bus einen Pin mit der PA-Enable-Leitung, darf diese nicht mehr per GPIO geschaltet werden. */
#if (CONFIG_IRM_DISPLAY_SSD1306 || CONFIG_IRM_DISPLAY_HD44780) && !defined(CONFIG_IRM_DISP_I2C_SHARED) && \
    (CONFIG_IRM_DISP_I2C_SDA == CONFIG_IRM_PA_GPIO || CONFIG_IRM_DISP_I2C_SCL == CONFIG_IRM_PA_GPIO)
#define IRM_PA_PIN_IS_I2C 1
#else
#define IRM_PA_PIN_IS_I2C 0
#endif

static void hp_task(void *arg)
{
    (void)arg;
    int stable = -1, cand = -1, cnt = 0;
    for (;;) {
        int lv = gpio_get_level(CONFIG_IRM_HP_DETECT_GPIO);
        if (lv == cand) { if (cnt < 3) cnt++; } else { cand = lv; cnt = 1; }
        if (cnt >= 3 && cand != stable) {
            stable = cand;
            bool hp = (stable == CONFIG_IRM_HP_ACTIVE_LEVEL);
            ESP_LOGI(TAG, "Kopfhoerer-Erkennung: GPIO%d=%d -> Kopfhoerer %s", CONFIG_IRM_HP_DETECT_GPIO, stable, hp ? "eingesteckt" : "nicht gesteckt");
#if CONFIG_IRM_HP_MUTE_SPEAKER && !IRM_PA_PIN_IS_I2C
            gpio_set_level(CONFIG_IRM_PA_GPIO, hp ? 0 : 1);   /* PA aktiv-high (pa_active_low = false) */
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

static bool s_inited;
static irm_out_sink_t *s_sink;
irm_out_sink_t *irm_board_sink(void) { return s_sink; }

#if CONFIG_IRM_OUT_CODEC
static esp_err_t init_codec(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;   /* Stille statt Wiederholung bei Underrun */
    cc.dma_frame_num = 480;
    cc.dma_desc_num = irm_out_dma_desc_num(CONFIG_IRM_AUDIO_OUT_RATE, CONFIG_IRM_AUDIO_DMA_MS, cc.dma_frame_num);
    const uint32_t buffer_ms = (uint32_t)((uint64_t)cc.dma_desc_num * cc.dma_frame_num * 1000u / CONFIG_IRM_AUDIO_OUT_RATE);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, NULL), TAG, "i2s chan");
    const i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_IRM_AUDIO_OUT_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = CONFIG_IRM_I2S_MCLK < 0 ? I2S_GPIO_UNUSED : CONFIG_IRM_I2S_MCLK,
            .bclk = CONFIG_IRM_I2S_BCLK, .ws = CONFIG_IRM_I2S_WS,
            .dout = CONFIG_IRM_I2S_DOUT, .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &sc), TAG, "i2s std");

    audio_codec_i2s_cfg_t ic = { .port = I2S_NUM_0, .tx_handle = s_tx, .rx_handle = NULL };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&ic);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    const audio_codec_if_t *codec_if = NULL;
    const audio_hw_pa_cfg_t pa = { .pa_pin = CONFIG_IRM_PA_GPIO, .pa_active_low = false };

#if CONFIG_IRM_CODEC_ES8388 || CONFIG_IRM_CODEC_ES8311
    audio_codec_i2c_cfg_t i2c_cfg = {
#if CONFIG_IRM_CODEC_ES8388
        .addr = ES8388_CODEC_DEFAULT_ADDR,
#else
        .addr = ES8311_CODEC_DEFAULT_ADDR,
#endif
        .bus_handle = s_i2c,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
#if CONFIG_IRM_CODEC_ES8388
    es8388_codec_cfg_t ec = { .ctrl_if = ctrl_if, .gpio_if = gpio_if, .pa_cfg = pa };
    codec_if = es8388_codec_new(&ec);
#else
    es8311_codec_cfg_t ec = { .ctrl_if = ctrl_if, .gpio_if = gpio_if, .pa_cfg = pa };
    codec_if = es8311_codec_new(&ec);
#endif
#else
    dummy_codec_cfg_t dc = { .gpio_if = gpio_if, .pa_cfg = pa };
    codec_if = dummy_codec_new(&dc);
#endif
    ESP_RETURN_ON_FALSE(data_if && codec_if, ESP_FAIL, TAG, "Codec-Schnittstelle nicht erzeugt");

    esp_codec_dev_cfg_t dev_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec_if, .data_if = data_if };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "esp_codec_dev_new");

    esp_codec_dev_sample_info_t fs = { .sample_rate = CONFIG_IRM_AUDIO_OUT_RATE, .channel = 2, .bits_per_sample = 16 };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "Codec oeffnen");
#if CONFIG_IRM_CODEC_ES8388
    /* Ausgangsstufen LOUT1/ROUT1/LOUT2/ROUT2 (Register 0x2E..0x31) gleich setzen: der Treiber laesst OUT2 bei -30 dB. */
    for (int reg = 0x2E; reg <= 0x31; reg++) {
        ESP_RETURN_ON_FALSE(esp_codec_dev_write_reg(s_codec, reg, CONFIG_IRM_ES8388_OUT_VOL) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "ES8388 Ausgangspegel");
    }
#endif
    ESP_RETURN_ON_ERROR(irm_out_codec_create(s_codec, CONFIG_IRM_AUDIO_OUT_RATE, buffer_ms, &s_sink), TAG, "codec sink");
    ESP_LOGI(TAG, "Ausgabe: Codec, %d Hz, Ausgabepuffer %u ms", CONFIG_IRM_AUDIO_OUT_RATE, (unsigned)buffer_ms);
#if CONFIG_IRM_HP_DETECT_GPIO >= 0
    const gpio_config_t hp_io = { .pin_bit_mask = 1ULL << CONFIG_IRM_HP_DETECT_GPIO, .mode = GPIO_MODE_INPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&hp_io), TAG, "hp gpio");
    xTaskCreate(hp_task, "irm_hp", 2560, NULL, 2, NULL);
#endif
    return ESP_OK;
}
#endif

esp_err_t irm_board_init(void)
{
    if (s_inited) return ESP_OK;

#if CONFIG_IRM_CODEC_ES8388 || CONFIG_IRM_CODEC_ES8311
    /* Codec-Bus (nur bei I2C-gesteuertem Codec) */
    const i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,   /* Codec: Controller 0 (wie im Altprojekt/ADF) */
        .sda_io_num = CONFIG_IRM_I2C_SDA,
        .scl_io_num = CONFIG_IRM_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &s_i2c), TAG, "codec i2c bus");
#endif

#if CONFIG_IRM_OUT_CODEC
    ESP_RETURN_ON_ERROR(init_codec(), TAG, "codec");
#elif CONFIG_IRM_OUT_PDM
    ESP_RETURN_ON_ERROR(irm_out_pdm_create(CONFIG_IRM_PDM_CLK, CONFIG_IRM_PDM_DOUT, CONFIG_IRM_AUDIO_OUT_RATE, CONFIG_IRM_AUDIO_DMA_MS, &s_sink), TAG, "pdm");
    ESP_LOGI(TAG, "Ausgabe: PDM auf GPIO%d (Mono, Software-Lautstaerke)", CONFIG_IRM_PDM_DOUT);
#elif CONFIG_IRM_OUT_DAC
#ifdef CONFIG_IRM_DAC_STEREO
    const bool dac_stereo = true;
#else
    const bool dac_stereo = false;
#endif
    ESP_RETURN_ON_ERROR(irm_out_dac_create(dac_stereo, CONFIG_IRM_AUDIO_OUT_RATE, CONFIG_IRM_AUDIO_DMA_MS, &s_sink), TAG, "dac");
    ESP_LOGI(TAG, "Ausgabe: interner DAC (GPIO25/26, %s, 8 Bit, Software-Lautstaerke)", dac_stereo ? "Stereo" : "Mono");
#endif
#if CONFIG_IRM_DISPLAY_SSD1306 || CONFIG_IRM_DISPLAY_HD44780
#ifdef CONFIG_IRM_DISP_I2C_SHARED
    s_disp_i2c = s_i2c;   /* Display am Codec-Bus */
#else
    /* Eigener Display-Bus (Altprojekt: I2C_NUM_1, GPIO21/22). Nach dem Codec angelegt: teilt sich das Display einen Pin mit
     * der PA-Enable-Leitung, uebernimmt der I2C-Treiber den Pin (Pull-up haelt die Endstufe aktiv). */
    const i2c_master_bus_config_t dbc = {
        .i2c_port = CONFIG_IRM_DISP_I2C_PORT,   /* Standard Controller 1: getrennt vom Codec */
        .sda_io_num = CONFIG_IRM_DISP_I2C_SDA,
        .scl_io_num = CONFIG_IRM_DISP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&dbc, &s_disp_i2c), TAG, "display i2c bus");
    ESP_LOGI(TAG, "Display-I2C: Controller %d, SDA=GPIO%d SCL=GPIO%d", CONFIG_IRM_DISP_I2C_PORT, CONFIG_IRM_DISP_I2C_SDA, CONFIG_IRM_DISP_I2C_SCL);
#if CONFIG_IRM_OUT_CODEC && CONFIG_IRM_PA_GPIO >= 0
    if (CONFIG_IRM_DISP_I2C_SDA == CONFIG_IRM_PA_GPIO || CONFIG_IRM_DISP_I2C_SCL == CONFIG_IRM_PA_GPIO) {
        ESP_LOGW(TAG, "GPIO%d dient als I2C-Leitung UND als PA-Enable: Endstufe wird vom I2C-Pull-up aktiv gehalten, "
                      "Kopfhoerer-Abschaltung der Endstufe ist wirkungslos", CONFIG_IRM_PA_GPIO);
    }
#endif
#endif
#endif
    s_inited = true;
    return ESP_OK;
}

esp_err_t irm_board_mount_sd(void)
{
#if !CONFIG_IRM_SD_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_card) return ESP_OK;
    const esp_vfs_fat_mount_config_t mc = { .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024 };
    esp_err_t e;
#if CONFIG_IRM_SD_SDMMC_1BIT
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    e = esp_vfs_fat_sdmmc_mount(IRM_SD_MOUNT_POINT, &host, &slot, &mc, &s_card);
#else
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    const spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_IRM_SD_SPI_MOSI, .miso_io_num = CONFIG_IRM_SD_SPI_MISO,
        .sclk_io_num = CONFIG_IRM_SD_SPI_SCLK, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA), TAG, "spi bus");
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = CONFIG_IRM_SD_SPI_CS;
    slot.host_id = host.slot;
    e = esp_vfs_fat_sdspi_mount(IRM_SD_MOUNT_POINT, &host, &slot, &mc, &s_card);
#endif
    if (e != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "SD-Karte nicht verfuegbar (%s)", esp_err_to_name(e));
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
#endif
}
