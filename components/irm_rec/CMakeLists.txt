idf_component_register(SRCS "src/irm_rec.c"
                       INCLUDE_DIRS "include"
                       REQUIRES irm_core irm_out
                       PRIV_REQUIRES irm_formats irm_board esp_timer esp_driver_gpio fatfs heap)
