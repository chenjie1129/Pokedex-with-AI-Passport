idf_component_register(
    SRCS
        "main.c"
        "ui_strings.c"
        "assets/city_font_14.c"
        "assets/city_font_20.c"
        "place_scan_coordinator.c"
        "pokemon_audio.c"
        "pokemon_content.c"
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

option(CITY_PLACE_PENDING_SMOKE "Exercise the new-place countdown without scanning or saves" OFF)
if(CITY_PLACE_PENDING_SMOKE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CITY_PLACE_PENDING_SMOKE=1)
endif()

option(CITY_PERSONALITY_SMOKE "Exercise individual personality without player-save mutations" OFF)
if(CITY_PERSONALITY_SMOKE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CITY_PERSONALITY_SMOKE=1)
endif()

option(CITY_CONTENT_SMOKE "Run no-save signed content workload on the device" OFF)
set(CITY_CONTENT_SMOKE_MS "1800000" CACHE STRING "Content diagnostic duration in milliseconds")
if(CITY_CONTENT_SMOKE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CITY_CONTENT_SMOKE=1 CITY_CONTENT_SMOKE_MS=${CITY_CONTENT_SMOKE_MS})
endif()
option(CITY_CONTENT_PERSIST_SMOKE "Verify package-only NVS in an isolated scratch namespace across reboot" OFF)
if(CITY_CONTENT_PERSIST_SMOKE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CITY_CONTENT_PERSIST_SMOKE=1)
endif()
