idf_component_register(SRCS "src/irm_web.c"
                       INCLUDE_DIRS "include"
                       REQUIRES irm_core
                       PRIV_REQUIRES esp_http_server irm_control cjson
                       EMBED_TXTFILES "www/index.html")
