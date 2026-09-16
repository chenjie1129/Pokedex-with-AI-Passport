set(STORE "${CMAKE_CURRENT_LIST_DIR}/../../../components/bsp/src/bsp_bestiary_store.c")
idf_component_register(SRCS "fault_test.c" "${STORE}"
    INCLUDE_DIRS "../../../components/bsp/include"
    REQUIRES city_domain nvs_flash app_update bootloader_support esp_partition spi_flash)
# Instrument only the production adapter in this standalone diagnostic build.
# The normal game and its builds do not contain these substitutions.
set_source_files_properties("${STORE}" PROPERTIES COMPILE_DEFINITIONS
    "nvs_open=fault_nvs_open;nvs_set_blob=fault_nvs_set_blob;nvs_commit=fault_nvs_commit;nvs_get_blob=fault_nvs_get_blob")
