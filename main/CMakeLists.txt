idf_component_register(
    SRCS
        "main.c"
        "place_scan_coordinator.c"
        "pokemon_audio.c"
        "assets/pokemon_cries.c"
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

option(CITY_AUDIO_RENDER_SMOKE "Exercise cries and descriptions without saves or input" OFF)
if(CITY_AUDIO_RENDER_SMOKE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CITY_AUDIO_RENDER_SMOKE=1)
endif()

option(CITY_SETTINGS_SMOKE "Exercise settings on device and restore preferences" OFF)
if(CITY_SETTINGS_SMOKE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CITY_SETTINGS_SMOKE=1)
endif()
