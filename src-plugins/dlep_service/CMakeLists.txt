# set library name
SET (libname "dlep_service")
SET (source ${CMAKE_CURRENT_SOURCE_DIR}/src/dlep_service.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/dlep_incoming.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/dlep_outgoing.c)

# use generic plugin cmake file
include (../CMake.plugin)
