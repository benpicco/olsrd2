# create application data for including
include_directories(${CMAKE_BINARY_DIR})

# generate full version string and initialization for static plugins
SET(GEN_DATA_C ${PROJECT_BINARY_DIR}/app_data.c)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app_data.h.in ${PROJECT_BINARY_DIR}/app_data.h)

ADD_CUSTOM_TARGET(AppCleanData ALL
    COMMAND ${CMAKE_COMMAND} -E remove ${GEN_DATA_C}
    COMMENT "Remove old builddata"
)

ADD_CUSTOM_COMMAND (
    OUTPUT ${GEN_DATA_C}
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/app_data.c.in
        -DDST=${GEN_DATA_C}
        -DGIT=${PROJECT_SOURCE_DIR}/.git
        -DOONF_APP=${OONF_APP}
        -DOONF_APP_VERSION=${OONF_APP_VERSION}
        -DOONF_VERSION_TRAILER=${OONF_VERSION_TRAILER}
        -DOONF_HELP_PREFIX=${OONF_HELP_PREFIX}
        -DOONF_HELP_SUFFIX=${OONF_HELP_SUFFIX}
        -DOONF_DEFAULT_CONF=${OONF_DEFAULT_CONF}
        -DOONF_APP_LIBPREFIX=${OONF_APP_LIBPREFIX}
        -DCMAKE_SYSTEM=${CMAKE_SYSTEM}
        -DCMAKE_SHARED_LIBRARY_PREFIX=${CMAKE_SHARED_LIBRARY_PREFIX}
        -DCMAKE_SHARED_LIBRARY_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}
        -DPLUGINS="${OONF_STATIC_PLUGINS}"
        -P ${PROJECT_SOURCE_DIR}/cmake/generate_builddata.cmake
    DEPENDS AppCleanData
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Create new builddata"
)
