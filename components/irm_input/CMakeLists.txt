idf_component_register(SRCS "src/irm_input.c"
                       INCLUDE_DIRS "include"
                       REQUIRES irm_core
                       PRIV_REQUIRES esp_driver_gpio esp_driver_pcnt esp_timer)
