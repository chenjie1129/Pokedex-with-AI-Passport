# Recompute identity when source contents, new files or Git state change.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB CITY_IDENTITY_ROOT CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/*")
file(GLOB_RECURSE CITY_IDENTITY_DISCOVERY CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/main/*" "${CMAKE_SOURCE_DIR}/components/*"
    "${CMAKE_SOURCE_DIR}/bootloader_components/*" "${CMAKE_SOURCE_DIR}/tools/*"
    "${CMAKE_SOURCE_DIR}/tests/*" "${CMAKE_SOURCE_DIR}/docs/*"
    "${CMAKE_SOURCE_DIR}/cmake/*"
    "${CMAKE_SOURCE_DIR}/demo/*")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/build_identity.py"
        --repo "${CMAKE_SOURCE_DIR}" --output-dir "${CMAKE_BINARY_DIR}/identity"
    RESULT_VARIABLE CITY_IDENTITY_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT CITY_IDENTITY_RESULT EQUAL 0)
    message(FATAL_ERROR "Cannot establish firmware source identity")
endif()
include("${CMAKE_BINARY_DIR}/identity/build-identity.cmake")
