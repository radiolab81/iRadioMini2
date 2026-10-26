idf_component_register(SRCS "src/irm_pcm.c" "src/irm_out.c" "src/irm_out_pdm.c" "src/irm_out_dac.c" "src/irm_out_codec.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_codec_dev esp_driver_i2s esp_driver_dac esp_driver_gpio)
