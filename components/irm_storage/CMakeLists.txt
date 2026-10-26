idf_component_register(SRCS "src/irm_settings.c" "src/irm_files.c"
                       INCLUDE_DIRS "include"
                       REQUIRES irm_core irm_formats nvs_flash
                       PRIV_REQUIRES esp_common)
