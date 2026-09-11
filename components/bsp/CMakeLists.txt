idf_component_register(
    SRCS "src/bsp_i2c.c"
         "src/bsp_display.c"
         "src/bsp_display_lvgl.c"
         "src/bsp_button.c"
         "src/bsp_audio.c"
         "src/bsp_battery.c"
         "src/bsp_wifi_scan.c"
         "src/bsp_bestiary_store.c"
         "src/bsp_settings_store.c"
         "src/bsp_place_identity.c"
         "src/bsp_place_store.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_driver_i2s esp_lcd esp_lvgl_port button esp_adc esp_codec_dev
             esp_wifi esp_netif esp_event esp_hw_support nvs_flash city_domain
)
