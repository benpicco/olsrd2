project(olsrd C)
cmake_minimum_required(VERSION 2.8)

include(./CMake.os)
include(./CMake.config)

# create all data inside the build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# set release specific compiler options
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g0 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -g0 -DNDEBUG")

# add generic compiler options
ADD_DEFINITIONS(-Werror)
include(./CMake.flags)

IF (OLSRD_REMOVE_DEBUG_LOGGING)
    ADD_DEFINITIONS(-DREMOVE_LOG_DEBUG)
ENDIF (OLSRD_REMOVE_DEBUG_LOGGING)
IF (OLSRD_REMOVE_INFO_LOGGING)
    ADD_DEFINITIONS(-DREMOVE_LOG_INFO)
ENDIF (OLSRD_REMOVE_INFO_LOGGING)
IF (OLSRD_REMOVE_WARN_LOGGING)
    ADD_DEFINITIONS(-DREMOVE_LOG_WARN)
ENDIF (OLSRD_REMOVE_WARN_LOGGING)

# OS-specific settings
IF(ANDROID OR WIN32)
    # Android and windows don't compile well with c99
    ADD_DEFINITIONS(-std=gnu99)
ELSE(ANDROID OR WIN32)
    # everything else does
    ADD_DEFINITIONS(-std=c99 -D_XOPEN_SOURCE=700 -D_BSD_SOURCE -D__BSD_VISIBLE -D_DARWIN_C_SOURCE -D__KERNEL_STRICT_NAMES)
ENDIF (ANDROID OR WIN32)

# overwrite OLSRd default config file for win32
IF(WIN32)
    ADD_DEFINITIONS(-DOLSRD_GLOBAL_CONF_FILE="olsrd.conf")
ENDIF(WIN32)

# add some necessary additions for win32
IF (WIN32)
    ADD_DEFINITIONS(-D_WIN32_WINNT=0x0502)
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--enable-auto-import")
ENDIF(WIN32)

# set include directories
include_directories(src)
include_directories(src/core)

# TODO: get rid of this
IF(WIN32)
    include_directories(src/win32)
ENDIF(WIN32)

IF(ANDROID)
    include_directories(src/android)
ENDIF(ANDROID)

# start compiling core and plugins
add_subdirectory(src)
add_subdirectory(lib)
