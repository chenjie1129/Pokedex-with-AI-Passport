cmake_minimum_required(VERSION 3.16)
project(city_spirits_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CITY_ENABLE_SANITIZERS "Enable address and undefined sanitizers" OFF)

enable_testing()

add_library(city_domain STATIC
    ../components/city_domain/src/bestiary_service.c
    ../components/city_domain/src/catalog_provider.c
    ../components/city_domain/src/content_pack.c
    ../components/city_domain/src/content_install.c
    ../components/city_domain/src/catalog_view.c
    ../components/city_domain/src/content_compat.c
    ../components/city_domain/src/capture_engine.c
    ../components/city_domain/src/encounter_selector.c
    ../components/city_domain/src/game_loop.c
    ../components/city_domain/src/location_mode.c
    ../components/city_domain/src/place_fingerprint.c
    ../components/city_domain/src/place_scan_policy.c
    ../components/city_domain/src/place_profile_codec.c
    ../components/city_domain/src/privacy_tokenizer.c
    ../components/city_domain/src/wild_reward_guard.c
    ../components/city_domain/src/pocket_policy.c
    ../components/city_domain/src/passport_progress.c
    ../components/city_domain/src/user_settings.c
)
target_include_directories(city_domain PUBLIC
    ../components/city_domain/include
)
target_compile_options(city_domain PRIVATE -Wall -Wextra -Werror -Wpedantic)

if(CITY_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(city_domain PRIVATE -fsanitize=address,undefined)
    target_link_options(city_domain PRIVATE -fsanitize=address,undefined)
endif()

function(add_city_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE city_domain)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic)
    if(CITY_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_city_test(test_privacy_tokenizer test_privacy_tokenizer.c)
add_city_test(test_place_fingerprint test_place_fingerprint.c)
add_city_test(test_location_mode test_location_mode.c)
add_city_test(test_wild_reward_guard test_wild_reward_guard.c)
add_city_test(test_capture_engine test_capture_engine.c)
add_city_test(test_encounter_selector test_encounter_selector.c)
add_city_test(test_bestiary_service test_bestiary_service.c)
add_city_test(test_game_loop test_game_loop.c)
add_city_test(city_mvp_scenario ../tools/city_mvp_scenario.c)

add_city_test(test_encounter_eligibility test_encounter_eligibility.c)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME test_build_identity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_build_identity.py")
add_city_test(test_pocket_features test_pocket_features.c)
add_city_test(test_battery_driver test_battery_driver.c)
target_include_directories(test_battery_driver PRIVATE
    battery_stubs ../components/bsp/include)
add_city_test(test_passport_progress test_passport_progress.c)
add_city_test(test_roster_expansion test_roster_expansion.c)
add_test(NAME test_species_catalog COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_species.py" --check)
add_test(NAME test_pokedex_research_sync
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_pokedex_research_sync.py")
add_test(NAME test_pokedex_research_snapshot
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/sync_pokedex_research.py" --check)
add_test(NAME test_pokedex_program
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_pokedex_program.py")
add_city_test(test_bestiary_adapter test_bestiary_adapter.c)
target_sources(test_bestiary_adapter PRIVATE ../components/bsp/src/bsp_bestiary_store.c)
target_include_directories(test_bestiary_adapter PRIVATE battery_stubs ../components/bsp/include)

add_city_test(test_buddy test_buddy.c)
add_city_test(test_personality test_personality.c)
add_city_test(test_memories test_memories.c)

add_city_test(test_evolution test_evolution.c)
add_city_test(test_health_and_release test_health_and_release.c)
add_city_test(test_user_settings test_user_settings.c)
target_sources(test_user_settings PRIVATE ../components/bsp/src/bsp_settings_store.c)
target_include_directories(test_user_settings PRIVATE battery_stubs ../components/bsp/include)
add_city_test(test_ui_strings test_ui_strings.c)
target_sources(test_ui_strings PRIVATE ../main/ui_strings.c)
target_include_directories(test_ui_strings PRIVATE ui_stubs ../main)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/settings_ui.c"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_settings_ui.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../main/main.c" "${CMAKE_CURRENT_BINARY_DIR}/settings_ui.c"
    DEPENDS ../main/main.c generate_settings_ui.py)
add_city_test(test_settings_ui "${CMAKE_CURRENT_BINARY_DIR}/settings_ui.c")

add_city_test(test_pokemon_audio test_pokemon_audio.c)
target_sources(test_pokemon_audio PRIVATE ../main/assets/pokemon_cries.c)
target_include_directories(test_pokemon_audio PRIVATE
    audio_stubs battery_stubs ../components/bsp/include ../main/assets)
add_test(NAME test_cries_assets COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_cries.py" --check)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/audio_transitions.c"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/audio_stubs/generate_transitions.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../main/main.c" "${CMAKE_CURRENT_BINARY_DIR}/audio_transitions.c"
    DEPENDS ../main/main.c audio_stubs/generate_transitions.py)
add_city_test(test_audio_transitions "${CMAKE_CURRENT_BINARY_DIR}/audio_transitions.c")

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/navigation_ui.c"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_navigation_ui.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../main/main.c" "${CMAKE_CURRENT_BINARY_DIR}/navigation_ui.c"
    DEPENDS ../main/main.c generate_navigation_ui.py navigation_ui_fixture.c)
add_city_test(test_navigation_ui "${CMAKE_CURRENT_BINARY_DIR}/navigation_ui.c")

add_test(NAME test_nvs_fault_log
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_nvs_fault_log.py")

add_test(NAME test_soak_evidence
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_soak_evidence.py")

add_test(NAME test_program_status
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_program_status.py")

find_program(CITY_OPENSSL openssl REQUIRED)
add_test(NAME test_content_pack
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_content_pack.py")

find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
add_executable(content_pack_reader_cli content_pack_reader_cli.c)
target_link_libraries(content_pack_reader_cli PRIVATE city_domain OpenSSL::Crypto)
target_compile_options(content_pack_reader_cli PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(CITY_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(content_pack_reader_cli PRIVATE -fsanitize=address,undefined)
    target_link_options(content_pack_reader_cli PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME test_content_pack_reader
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_content_pack_reader.py"
            "$<TARGET_FILE:content_pack_reader_cli>")

# Actual device crypto adapter also runs on the host (mbedTLS 2.28 or 3.x).
# Linux: libmbedtls-dev; macOS: mbedTLS prefix via CMAKE_PREFIX_PATH.
find_path(CITY_MBEDTLS_INCLUDE mbedtls/ecdsa.h REQUIRED)
find_library(CITY_MBEDCRYPTO mbedcrypto REQUIRED)
add_library(city_content_crypto STATIC ../components/bsp/src/bsp_content_crypto.c)
target_include_directories(city_content_crypto PUBLIC ../components/bsp/include ${CITY_MBEDTLS_INCLUDE})
target_link_libraries(city_content_crypto PUBLIC city_domain ${CITY_MBEDCRYPTO})
target_compile_options(city_content_crypto PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(CITY_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(city_content_crypto PRIVATE -fsanitize=address,undefined)
endif()
add_executable(test_content_install test_content_install.c)
target_link_libraries(test_content_install PRIVATE city_content_crypto)
target_compile_options(test_content_install PRIVATE -Wall -Wextra -Werror -Wpedantic)
if(CITY_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_content_install PRIVATE -fsanitize=address,undefined)
    target_link_options(test_content_install PRIVATE -fsanitize=address,undefined)
endif()
# Python generates ephemeral P-256 keys and typed packs, then calls the C fixture.
add_test(NAME test_content_device_crypto
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_content_device_crypto.py"
            "$<TARGET_FILE:test_content_install>")
add_city_test(test_content_storage test_content_storage.c)
target_sources(test_content_storage PRIVATE ../components/bsp/src/bsp_content_storage.c)
target_include_directories(test_content_storage PRIVATE content_stubs ../components/bsp/include)
add_test(NAME test_content_compat
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_content_compat.py"
            "$<TARGET_FILE:test_content_install>")
