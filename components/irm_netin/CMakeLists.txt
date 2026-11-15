# Quellen bedingt, REQUIRES immer gleich (CONFIG_* ist in der fruehen Abhaengigkeitsanalyse noch nicht verfuegbar).
set(srcs "")
if(CONFIG_IRM_NETIN_ENABLE)
    list(APPEND srcs "src/irm_netin.c" "src/irm_netin_policy.c")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES irm_out irm_core
                       PRIV_REQUIRES esp_netif lwip esp_timer)
