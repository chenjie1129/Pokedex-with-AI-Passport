idf_component_register(
    SRCS
        "device_test.c"
        "../../../components/bsp/src/bsp_bestiary_store.c"
    INCLUDE_DIRS
        "../../../components/bsp/include"
    REQUIRES city_domain nvs_flash
)
