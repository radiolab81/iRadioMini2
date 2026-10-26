/* AD9850: 40-Bit-Seriell-Ladung (Frequenzwort LSB zuerst, danach Steuer-/Phasenbyte), W_CLK steigende Flanke, FQ_UD uebernimmt. */
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"
#include "irm_tx_driver.h"

#define RST_LINE  CONFIG_IRM_TX_PIN_A
#define FQ_LINE   CONFIG_IRM_TX_PIN_B
#define CLK_LINE  CONFIG_IRM_TX_PIN_C
#define DATA_LINE CONFIG_IRM_TX_PIN_D

static void pulse(int pin) { gpio_set_level(pin, 1); esp_rom_delay_us(2); gpio_set_level(pin, 0); esp_rom_delay_us(2); }

static void send_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        gpio_set_level(DATA_LINE, (b >> i) & 1);
        pulse(CLK_LINE);
    }
}

static esp_err_t set_freq_hz(uint32_t hz)
{
    uint32_t fw = (uint32_t)(((uint64_t)hz << 32) / CONFIG_IRM_TX_DDS_MCLK_HZ);
    for (int i = 0; i < 4; i++) send_byte((uint8_t)(fw >> (8 * i)));   /* w4..w1: LSB zuerst */
    send_byte(0x00);                                                   /* w0: keine Phase, Power-up */
    pulse(FQ_LINE);
    return ESP_OK;
}

static esp_err_t init(void)
{
    const gpio_config_t io = { .pin_bit_mask = (1ULL << RST_LINE) | (1ULL << FQ_LINE) | (1ULL << CLK_LINE) | (1ULL << DATA_LINE),
                               .mode = GPIO_MODE_OUTPUT };
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) return e;
    gpio_set_level(RST_LINE, 0); gpio_set_level(FQ_LINE, 0); gpio_set_level(CLK_LINE, 0); gpio_set_level(DATA_LINE, 0);
    pulse(RST_LINE); pulse(CLK_LINE); pulse(FQ_LINE);                  /* Reset + in Seriell-Modus wechseln */
    return ESP_OK;
}

const irm_tx_driver_t irm_tx_driver = { "AD9850", init, set_freq_hz };
