/* Ausgabesenken fuer Boards ohne externen Codec/DAC (Minimalziel wie in iRadioMini V1: USE_INTERNAL_AUDIODAC).
 * Eine Senke nimmt 16-Bit-Stereo-PCM mit der Pipeline-Samplerate entgegen; Lautstaerke wird in Software angewandt. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct irm_out_sink irm_out_sink_t;
struct irm_out_sink {
    const char *name;
    esp_err_t (*open)(irm_out_sink_t *sink);    /* Ausgabe starten (bei jedem Pipeline-Lauf) */
    esp_err_t (*write)(irm_out_sink_t *sink, const int16_t *stereo, size_t frames);   /* blockiert bis uebernommen */
    void      (*close)(irm_out_sink_t *sink);
    volatile uint32_t gain_q15;                 /* Software-Lautstaerke (irm_out_set_volume) */
    uint32_t sample_rate;                       /* Pipeline-Samplerate (Hz) */
    uint32_t buffer_ms;                         /* Tiefe des Ausgabepuffers (DMA) in ms - Grundlage der Aussetzer-Erkennung */
};

/** PDM-Ausgabe ueber I2S0 (ESP32): ein Datenpin, Mono-Downmix; extern RC-Tiefpass + Verstaerker. clk_gpio < 0: ungenutzt. */
esp_err_t irm_out_pdm_create(int clk_gpio, int dout_gpio, uint32_t sample_rate, uint32_t buffer_ms, irm_out_sink_t **out);
/** Interner 8-Bit-DAC (ESP32: GPIO25 = Kanal 0, GPIO26 = Kanal 1). stereo=false: Mono-Signal auf beiden Pins. */
esp_err_t irm_out_dac_create(bool stereo, uint32_t sample_rate, uint32_t buffer_ms, irm_out_sink_t **out);
/** Codec-Ausgabe: schreibt per esp_codec_dev_write. Die Lautstaerke bleibt beim Codec (Hardware); der Codec muss geoeffnet sein. */
esp_err_t irm_out_codec_create(void *codec_dev_handle, uint32_t sample_rate, uint32_t buffer_ms, irm_out_sink_t **out);
/** Anzahl I2S-DMA-Deskriptoren (je dma_frame_num Frames) fuer eine Puffertiefe in ms. */
uint32_t irm_out_dma_desc_num(uint32_t sample_rate, uint32_t buffer_ms, uint32_t dma_frame_num);

void irm_out_set_volume(irm_out_sink_t *sink, uint8_t percent);
#ifdef __cplusplus
}
#endif
