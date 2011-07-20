# generate full version string and initialization for static plugins
SET(GEN_DATA_C           ${PROJECT_BINARY_DIR}/data.c)
SET(GEN_PLUGIN_STATIC_C  ${PROJECT_BINARY_DIR}/plugin_static.c)

ADD_CUSTOM_TARGET(OlsrdCleanData ALL
    COMMAND ${CMAKE_COMMAND} -E remove ${GEN_BUILDDATA_H} ${GEN_PLUGIN_STATIC_C}
    COMMENT "Remove old builddata"
)

ADD_CUSTOM_COMMAND (
    OUTPUT ${GEN_DATA_C} ${GEN_PLUGIN_STATIC_C}
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/data.c.in
        -DDST=${GEN_DATA_C}
        -DGIT=${PROJECT_SOURCE_DIR}/.git
        -DOLSRD_VERSION=${OLSRD_VERSION}
        -DCMAKE_SYSTEM=${CMAKE_SYSTEM}
        -DCMAKE_SHARED_LIBRARY_PREFIX=${CMAKE_SHARED_LIBRARY_PREFIX}
        -DCMAKE_SHARED_LIBRARY_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_builddata.cmake
    COMMAND ${CMAKE_COMMAND}
        -DDST=${GEN_PLUGIN_STATIC_C}
        -DPLUGINS="${OLSRD_STATIC_PLUGINS}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_static_loader.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Create new builddata"
)

# create static library with the generated data
ADD_LIBRARY(OlsrdBuildData STATIC ${GEN_DATA_C} ${GEN_PLUGIN_STATIC_C})
ADD_DEPENDENCIES(OlsrdBuildData OlsrdCleanData)
