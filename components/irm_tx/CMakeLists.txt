set(srcs "src/irm_tx.c")   # immer: ohne CONFIG_IRM_TX_ENABLE liefert irm_tx_start() ESP_ERR_NOT_SUPPORTED
if(CONFIG_IRM_TX_ENABLE)
    if(CONFIG_IRM_TX_AD9835 OR CONFIG_IRM_TX_AD9832)
        list(APPEND srcs "src/irm_tx_ad983x.c")
    elseif(CONFIG_IRM_TX_AD9850)
        list(APPEND srcs "src/irm_tx_ad9850.c")
    elseif(CONFIG_IRM_TX_LEDC)
        list(APPEND srcs "src/irm_tx_ledc.c")
    endif()
    if(CONFIG_IRM_INET2RF)
        list(APPEND srcs "src/irm_inet2rf.c")
    endif()
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES irm_core
                       PRIV_REQUIRES irm_storage esp_driver_gpio esp_driver_ledc esp_driver_pcnt esp_timer esp_rom)
