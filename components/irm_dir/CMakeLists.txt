# Quellen bedingt, aber REQUIRES immer gleich: CONFIG_* ist in der fruehen Abhaengigkeitsanalyse noch nicht verfuegbar.
set(srcs "src/irm_dir_query.c")
if(CONFIG_IRM_DIR_ENABLE)
    list(APPEND srcs "src/irm_dir.c" "src/irm_dir_store.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES kiwi_dir irm_formats
                       PRIV_REQUIRES esp_partition esp_timer)
