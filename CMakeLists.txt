project (OONF C)
cmake_minimum_required(VERSION 2.8)

###################################
#### Application configuration ####
###################################

# set library version
set (OONF_MAJOR_VERSION 0)
set (OONF_MINOR_VERSION 7)
set (OONF_PATCH_VERSION 0)

# calculate complete library version
set (OONF_VERSION ${OONF_MAJOR_VERSION}.${OONF_MINOR_VERSION}.${OONF_PATCH_VERSION})

# set cached variables
include (./cmake/app_config.cmake)

# include compiler flags
include (./cmake/cc_flags.cmake)

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

if (OONF_API_DIR)
    set (OONF_DIR "${OONF_API_DIR}/build")
endif (OONF_API_DIR)

find_package(OONF REQUIRED)
if (NOT OONF_FOUND)
    message(FATAL_ERROR "Cannot compile application without OONF framework")
endif (NOT OONF_FOUND)

message ("Include directory: ${OONF_INCLUDE_DIRS}") 
include_directories(${OONF_INCLUDE_DIRS})
include(${OONF_BUILD_LIBRARY})

#########################
#### Compile targets ####
#########################

# set include directory
include_directories(${PROJECT_BINARY_DIR})
include_directories (src)
include_directories (src-plugins)

# get additional build directories
add_subdirectory (src)
add_subdirectory (src-plugins)
