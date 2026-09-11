idf_component_register(
    SRCS
        "device_test.c"
        "../../../components/bsp/src/bsp_place_identity.c"
        "../../../components/bsp/src/bsp_place_store.c"
    INCLUDE_DIRS
        "../../../components/bsp/include"
    REQUIRES city_domain esp_hw_support nvs_flash
)
