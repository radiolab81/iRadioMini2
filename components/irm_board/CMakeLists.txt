idf_component_register(SRCS "src/irm_board.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_codec_dev esp_driver_i2c irm_out
                       PRIV_REQUIRES esp_driver_i2s esp_driver_gpio esp_driver_sdmmc
                                     esp_driver_sdspi esp_driver_spi fatfs sdmmc vfs)
