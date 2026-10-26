#pragma once
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "irm_out.h"

#ifdef __cplusplus
extern "C" {
#endif
#define IRM_SD_MOUNT_POINT "/sdcard"

/** Ausgabe initialisieren: je nach Kconfig Codec (I2C+I2S, Codec geoeffnet mit CONFIG_IRM_AUDIO_OUT_RATE, 2ch, 16bit),
 *  PDM-Senke oder interner DAC. Busse werden nur angelegt, wenn Codec bzw. I2C-Display sie benoetigen. */
esp_err_t irm_board_init(void);
/** I2C-Bus des Codecs (nur ES8388/ES8311); sonst NULL. */
i2c_master_bus_handle_t irm_board_i2c(void);
/** I2C-Bus fuer I2C-Displays: eigener Bus (Standard GPIO21/22, Port automatisch) oder - per Kconfig - der Codec-Bus. NULL ohne I2C-Display. */
i2c_master_bus_handle_t irm_board_display_i2c(void);
/** Codec-Ausgabe (NULL bei PDM/DAC). */
/** Beschreibung des Display-Busses fuer Logmeldungen ("Controller 1, SDA=GPIO21, SCL=GPIO22" bzw. "Codec-Bus"). */
const char *irm_board_display_i2c_desc(void);
esp_codec_dev_handle_t  irm_board_codec(void);
/** Senke fuer PDM/DAC (NULL bei Codec-Ausgabe). Genau eines von codec()/sink() ist ungleich NULL. */
irm_out_sink_t         *irm_board_sink(void);
/** SD-Karte mounten. ESP_ERR_NOT_FOUND wenn keine Karte steckt (kein Fehler fuer die App). */
esp_err_t irm_board_mount_sd(void);
bool      irm_board_sd_mounted(void);
#ifdef __cplusplus
}
#endif
