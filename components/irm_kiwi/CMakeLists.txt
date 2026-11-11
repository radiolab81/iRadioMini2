# Quellen bedingt, aber REQUIRES immer gleich: CONFIG_* ist in der fruehen Abhaengigkeitsanalyse noch nicht verfuegbar.
# (Ohne IRM_KIWI_ENABLE bleibt die Komponente ein leerer Header-Rahmen; kiwi_core wird dann zwar gebaut, aber nicht gelinkt.)
set(srcs "")
if(CONFIG_IRM_KIWI_ENABLE)
    list(APPEND srcs "src/irm_kiwi.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES irm_out irm_core
                       PRIV_REQUIRES irm_formats kiwi_core esp_timer)
