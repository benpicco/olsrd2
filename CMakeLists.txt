project(OONF_APP C)
cmake_minimum_required(VERSION 2.8)

set(OONF_CONFIG ./CMake.config)

include(${OONF_CONFIG})
include(./CMake.os)

# create all data inside the build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# set release specific compiler options
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O3 -g0 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -g0 -DNDEBUG")

# add generic compiler options
ADD_DEFINITIONS(-Werror)
include(./CMake.flags)

# add a target to generate API documentation with Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/framework.doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/framework.doxyfile @ONLY)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/framework.doxyfile
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
endif(DOXYGEN_FOUND)

# set include directories
include_directories(src)
include_directories(src-api)
include_directories(src-api/core)
include_directories(${CMAKE_BINARY_DIR})

ENABLE_TESTING()

# start compiling core and plugins
add_subdirectory(src)
add_subdirectory(src-api)
add_subdirectory(src-plugins)
add_subdirectory(tests)
