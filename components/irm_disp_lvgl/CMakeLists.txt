# Quellen/Assets bedingt, REQUIRES immer gleich (CONFIG_* fehlt in der fruehen Abhaengigkeitsanalyse).
set(srcs "")
set(embed "")
if(CONFIG_IRM_DISPLAY_LVGL)
    list(APPEND srcs "src/irm_disp_lvgl.c" "src/irm_assets.c")
    if(CONFIG_IRM_LVGL_SKIN_BASIC)
        list(APPEND srcs "src/skin_basic.c")
    elseif(CONFIG_IRM_LVGL_SKIN_LINEAR)
        list(APPEND srcs "src/skin_linear.c")
        list(APPEND embed "assets/scale_linear.bin" "assets/needle_linear.bin")
    elseif(CONFIG_IRM_LVGL_SKIN_ROUND)
        list(APPEND srcs "src/skin_round.c")
        list(APPEND embed "assets/scale_round.bin" "assets/needle_round.bin")
    elseif(CONFIG_IRM_LVGL_SKIN_CASSETTE)
        list(APPEND srcs "src/skin_cassette.c")
        list(APPEND embed "assets/cassette.bin" "assets/reel.bin")
    endif()
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src" "assets"
                       REQUIRES irm_display
                       PRIV_REQUIRES irm_core esp_lcd esp_driver_spi esp_driver_gpio
                       EMBED_FILES ${embed})

# LVGL-Pakete nur bei gewaehltem LVGL-Backend anfordern (Abhaengigkeiten werden bei Bedarf automatisch geladen).
if(CONFIG_IRM_DISPLAY_LVGL)
    idf_component_optional_requires(PRIVATE esp_lvgl_port)
endif()

# ILI9341 ist kein Teil von ESP-IDF 6.1 (verwaltete Komponente espressif/esp_lcd_ili9341); ST7789 und SSD1306 sind im Kern.
if(CONFIG_IRM_LCD_ILI9341)
    idf_component_optional_requires(PRIVATE esp_lcd_ili9341)
endif()
