idf_component_register(
    SRCS
        "main.c"
        "place_scan_coordinator.c"
        "assets/charmander_sprite.c"
        "assets/starter_sprites.c"
        "assets/roster_sprites.c"
    INCLUDE_DIRS
        "."
        "assets"
        "${CMAKE_BINARY_DIR}/identity"
    REQUIRES
        bsp
        city_domain
        esp_timer
        nvs_flash
)

option(CITY_CAPTURE_RENDER_SMOKE "Run capture rendering on device without saves or input" OFF)
if(CITY_CAPTURE_RENDER_SMOKE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CITY_CAPTURE_RENDER_SMOKE=1)
endif()
