# set library name
SET (libname "dlep_client")
SET (source ${CMAKE_CURRENT_SOURCE_DIR}/src/dlep_client.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/dlep_client_incoming.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/dlep_client_outgoing.c
            )

# use generic plugin cmake file
include (../CMake.plugin)
