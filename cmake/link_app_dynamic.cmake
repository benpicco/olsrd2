# link plugin loader
TARGET_LINK_LIBRARIES(${OONF_EXE} static_pluginloader)

# link static plugins
FOREACH(plugin ${OONF_STATIC_PLUGINS})
    IF(TARGET oonf_static_${plugin})
        TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_static_${plugin})
    ELSE (TARGET oonf_static_${plugin})
        TARGET_LINK_LIBRARIES(${OONF_EXE} ${OONF_APP_LIBPREFIX}_static_${plugin})
    ENDIF(TARGET oonf_static_${plugin})
ENDFOREACH(plugin)

# link core
TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_core)

# link packetbb if necessary
IF(OONF_NEED_PACKETBB)
    TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_packetbb)
ENDIF(OONF_NEED_PACKETBB)

# link config and common API
TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_config)
TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_common)

# link builddata
TARGET_LINK_LIBRARIES(${OONF_EXE} static_builddata)

# link dlopen() library
TARGET_LINK_LIBRARIES(${OONF_EXE} ${CMAKE_DL_LIBS})

# link extra win32 libs
IF(WIN32)
    TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_regex)

    SET_TARGET_PROPERTIES(${OONF_EXE} PROPERTIES ENABLE_EXPORTS true)
    TARGET_LINK_LIBRARIES(${OONF_EXE} ws2_32 iphlpapi)
ENDIF(WIN32)
