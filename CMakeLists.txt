project(Test C)
cmake_minimum_required(VERSION 2.8)

# load application configuration
set(OONF_CONFIG_APPLICATION ./app_config.cmake CACHE PATH "Configuration file for application")
include(${OONF_CONFIG_APPLICATION})

# use libraries flag handling
include (./cmake/cc_flags.cmake)

SET (OONF_DIR /home/rogge/develop/olsrd/olsrd-api)
find_package(OONF REQUIRED)
if (NOT OONF_FOUND)
    message(FATAL_ERROR "Cannot compile application without OONF framework")
endif (NOT OONF_FOUND)

include_directories(${OONF_INCLUDE_DIRS})
include(${OONF_BUILD_LIBRARY})

# set include directory
include_directories (src)
include_directories (src-plugins)

# get additional build directories
add_subdirectory (src)
add_subdirectory (src-plugins)
