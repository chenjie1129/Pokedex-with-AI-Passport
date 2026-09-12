cmake_minimum_required(VERSION 3.16)
project(passport_render C)
set(CMAKE_C_STANDARD 11)
get_filename_component(ROOT "../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/managed_components/lvgl__lvgl" lvgl)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/render.c"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate.py" "${ROOT}/main/main.c" "${CMAKE_CURRENT_BINARY_DIR}/render.c"
    DEPENDS "${ROOT}/main/main.c" generate.py harness.c)
add_executable(passport_render "${CMAKE_CURRENT_BINARY_DIR}/render.c"
    "${ROOT}/components/city_domain/src/passport_progress.c"
    "${ROOT}/components/city_domain/src/capture_engine.c"
    "${ROOT}/components/city_domain/src/user_settings.c"
    "${ROOT}/components/city_domain/src/bestiary_service.c"
    "${ROOT}/components/city_domain/src/wild_reward_guard.c"
    "${ROOT}/components/city_domain/src/pocket_policy.c")
target_sources(passport_render PRIVATE "${ROOT}/main/assets/city_font_14.c" "${ROOT}/main/assets/city_font_20.c" "${ROOT}/main/assets/charmander_sprite.c" "${ROOT}/main/assets/starter_sprites.c" "${ROOT}/main/assets/roster_sprites.c")
target_include_directories(passport_render PRIVATE "${ROOT}/components/city_domain/include" "${ROOT}/main" "${ROOT}/main/assets")
target_link_libraries(passport_render PRIVATE lvgl)
