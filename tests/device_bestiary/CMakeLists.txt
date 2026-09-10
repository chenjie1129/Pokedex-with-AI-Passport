cmake_minimum_required(VERSION 3.16)

get_filename_component(
    REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS
    "${REPOSITORY_ROOT}/components/city_domain")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pokemon_bestiary_device_test)
