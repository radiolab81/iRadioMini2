idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES irm_core irm_formats irm_storage irm_board irm_net irm_audio
                                     irm_control irm_input irm_display irm_web
                                     irm_disp_ssd1306 irm_disp_hd44780 irm_disp_servo irm_disp_lvgl irm_tx)
