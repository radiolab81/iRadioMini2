idf_component_register(SRCS "src/irm_web.c" "src/irm_web_dir.c"
                       INCLUDE_DIRS "include"
                       REQUIRES irm_core
                       PRIV_REQUIRES esp_http_server irm_control irm_formats irm_dir cjson
                       EMBED_TXTFILES "www/index.html")
