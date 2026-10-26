# Quellen bedingt, aber REQUIRES immer gleich: CONFIG_* ist in der fruehen Abhaengigkeitsanalyse noch nicht verfuegbar.
set(srcs "")
if(CONFIG_IRM_DISPLAY_HD44780)
    list(APPEND srcs "src/irm_disp_hd44780.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES irm_display irm_board esp_driver_i2c
                       PRIV_REQUIRES esp_rom)
