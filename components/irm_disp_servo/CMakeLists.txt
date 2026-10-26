# Quellen bedingt, aber REQUIRES immer gleich: CONFIG_* ist in der fruehen Abhaengigkeitsanalyse noch nicht verfuegbar.
set(srcs "")
if(CONFIG_IRM_DISPLAY_SERVO)
    list(APPEND srcs "src/irm_disp_servo.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES irm_display
                       PRIV_REQUIRES esp_driver_ledc)
