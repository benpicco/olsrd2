project (OONF C)
cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)

###################################
#### Application configuration ####
###################################

# set OONF library version
set (OONF_VERSION "0.1")

# set cached variables
include (./cmake/app_config.cmake)

# include compiler flags
include (./cmake/cc_flags.cmake)

# get app version from git tag
include (./cmake/generate_version.cmake)
message ("Library version: ${OONF_VERSION}")

########################
#### Doxygen target ####
########################

find_package(Doxygen)
if(DOXYGEN_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/files/olsrv2.doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/olsrv2.doxyfile @ONLY)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/olsrv2.doxyfile
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating OLSRv2 documentation with Doxygen" VERBATIM
    )
endif(DOXYGEN_FOUND)

###############################
#### Include API framework ####
###############################

if (OONF_DO_NOT_REGISTER_PACKAGE)
    message ("Looking for OONF-API: ${CMAKE_PREFIX_PATH}")
    find_package(OONF NO_CMAKE_ENVIRONMENT_PATH
                      NO_SYSTEM_ENVIRONMENT_PATH
                      NO_CMAKE_PACKAGE_REGISTRY
                      NO_CMAKE_BUILDS_PATH
                      NO_CMAKE_SYSTEM_PATH
                      NO_CMAKE_SYSTEM_PACKAGE_REGISTRY)
else (OONF_DO_NOT_REGISTER_PACKAGE)
    find_package(OONF REQUIRED)
endif (OONF_DO_NOT_REGISTER_PACKAGE)

if (NOT OONF_FOUND)
    message(FATAL_ERROR "Cannot compile application without OONF framework")
endif (NOT OONF_FOUND)

include_directories(${OONF_INCLUDE_DIRS})
include(${OONF_BUILD_LIBRARY})

#########################
#### Compile targets ####
#########################

# set include directory
include_directories(${PROJECT_BINARY_DIR})
include_directories (src-plugins)
include_directories (src)

# get additional build directories
add_subdirectory (src-plugins)
add_subdirectory (src)
