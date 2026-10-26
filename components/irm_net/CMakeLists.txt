idf_component_register(SRCS "src/irm_net.c"
                       INCLUDE_DIRS "include"
                       REQUIRES irm_core
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer)
