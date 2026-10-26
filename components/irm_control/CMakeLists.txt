idf_component_register(SRCS "src/irm_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES irm_core irm_formats
                       PRIV_REQUIRES heap irm_audio irm_storage irm_board esp_timer)
