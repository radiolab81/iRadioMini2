idf_component_register(SRCS "src/irm_audio.c" "src/irm_io_sink.c" "src/irm_io_m4a.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES irm_core irm_out esp_codec_dev
                       PRIV_REQUIRES irm_formats heap esp_timer gmf_core gmf_audio gmf_io esp_http_client esp-tls mbedtls)
