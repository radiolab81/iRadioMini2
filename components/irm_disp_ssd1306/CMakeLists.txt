# Quellen bedingt. REQUIRES der immer vorhandenen Komponenten statisch (CONFIG_* fehlt in der fruehen Abhaengigkeitsanalyse);
# u8g2 wird nur bei gewaehltem SSD1306 angefordert - die Git-Abhaengigkeit (idf_component.yml) wird dann vom Component Manager automatisch geladen.
set(srcs "")
if(CONFIG_IRM_DISPLAY_SSD1306)
    list(APPEND srcs "src/irm_disp_ssd1306.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES irm_display irm_board esp_driver_i2c
                       PRIV_REQUIRES esp_rom)
if(CONFIG_IRM_DISPLAY_SSD1306)
    idf_component_optional_requires(PRIVATE u8g2)
endif()
