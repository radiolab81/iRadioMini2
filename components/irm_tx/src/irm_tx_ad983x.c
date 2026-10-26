/* AD9832/AD9835: 16-Bit-Seriell-Worte, MSB zuerst, SCLK-Ruhepegel high, Daten mit fallender Flanke uebernommen.
 * Protokoll und Registeradressen unveraendert aus dem Altprojekt; Bit-Schieben als Schleife statt 16x ausgerollt. */
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"
#include "irm_tx_driver.h"

#define FSYNC CONFIG_IRM_TX_PIN_A
#define SCLK  CONFIG_IRM_TX_PIN_B
#define SDATA CONFIG_IRM_TX_PIN_C

#define CMD_WRI16_PHASE_REG        0x0000
#define CMD_WRI8_PHASE_DEFER_REG   0x1000
#define CMD_WRI16_FREQ_REG         0x2000
#define CMD_WRI8_FREQ_DEFER_REG    0x3000
#define CMD_SELECT_PHASE_REG       0x4000
#define CMD_SELECT_FREQ_REG        0x5000
#define CMD_SET_SYNC_AND_SELSRC    0x8000
#define CMD_SET_SLEEP_RST_CLR      0xC000
#define SYNC_HIGH                  0x2000
#define SELSRC_HIGH                0x1000
#define SLEEP_HIGH                 0x2000
#define RESET_HIGH                 0x1000
#define CLR_HIGH                   0x0800
#define FREQ0_REG_8_L_LSB          0x000
#define FREQ0_REG_8_H_LSB          0x100
#define FREQ0_REG_8_L_MSB          0x200
#define FREQ0_REG_8_H_MSB          0x300
#define PHASE0_REG_8_LSB           0x800
#define PHASE0_REG_8_MSB           0x900

static void send_word(uint16_t w)
{
    gpio_set_level(SCLK, 1);
    gpio_set_level(FSYNC, 1);
    gpio_set_level(FSYNC, 0);
    for (int bit = 15; bit >= 0; bit--) {
        gpio_set_level(SDATA, (w >> bit) & 1);
        gpio_set_level(SCLK, 1);
        gpio_set_level(SCLK, 0);
        gpio_set_level(SCLK, 1);
    }
    gpio_set_level(FSYNC, 1);
}

static esp_err_t set_freq_hz(uint32_t hz)
{
    uint32_t fw = (uint32_t)(((uint64_t)hz << 32) / CONFIG_IRM_TX_DDS_MCLK_HZ);   /* Abstimmwort */
    send_word((uint16_t)(CMD_WRI8_FREQ_DEFER_REG | FREQ0_REG_8_L_LSB | (fw & 0xFF)));
    send_word((uint16_t)(CMD_WRI16_FREQ_REG      | FREQ0_REG_8_H_LSB | ((fw >> 8) & 0xFF)));
    send_word((uint16_t)(CMD_WRI8_FREQ_DEFER_REG | FREQ0_REG_8_L_MSB | ((fw >> 16) & 0xFF)));
    send_word((uint16_t)(CMD_WRI16_FREQ_REG      | FREQ0_REG_8_H_MSB | ((fw >> 24) & 0xFF)));
    return ESP_OK;
}

static esp_err_t init(void)
{
    const gpio_config_t io = { .pin_bit_mask = (1ULL << FSYNC) | (1ULL << SCLK) | (1ULL << SDATA), .mode = GPIO_MODE_OUTPUT };
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK) return e;
    send_word(CMD_SET_SLEEP_RST_CLR | SLEEP_HIGH | RESET_HIGH | CLR_HIGH);
    send_word(CMD_SET_SYNC_AND_SELSRC | SYNC_HIGH | SELSRC_HIGH);
    send_word((uint16_t)(CMD_WRI8_PHASE_DEFER_REG | PHASE0_REG_8_LSB));   /* Phase 0 */
    send_word((uint16_t)(CMD_WRI16_PHASE_REG | PHASE0_REG_8_MSB));
    send_word(CMD_SELECT_FREQ_REG);
    send_word(CMD_SELECT_PHASE_REG);
    send_word(CMD_SET_SLEEP_RST_CLR);                                      /* Sleep/Reset aufheben */
    return ESP_OK;
}

const irm_tx_driver_t irm_tx_driver = {
#if CONFIG_IRM_TX_AD9832
    "AD9832",
#else
    "AD9835",
#endif
    init, set_freq_hz };
