# link static plugins
string(REPLACE " " ";" PLUGIN_LIST "${OONF_STATIC_PLUGINS}")
message ("Plugins: ${PLUGIN_LIST}")
FOREACH(plugin ${PLUGIN_LIST})
    IF(TARGET oonf_static_${plugin})
        message ("    Found target: oonf_static_${plugin}")  
        TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive oonf_static_${plugin} -Wl,--no-whole-archive)
    ELSEIF (TARGET ${OONF_APP_LIBPREFIX}_static_${plugin})
        message ("    Found target: ${OONF_APP_LIBPREFIX}_static_${plugin}")  
        TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive ${OONF_APP_LIBPREFIX}_static_${plugin} -Wl,--no-whole-archive)
    ELSE (TARGET oonf_static_${plugin})
        message (FATAL_ERROR "    Did not found target: oonf_static_${plugin} or ${OONF_APP_LIBPREFIX}_static_${plugin}")
    ENDIF(TARGET oonf_static_${plugin})
ENDFOREACH(plugin)

# link subsystems
TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_subsystems)

# link core
TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_core)

# link packetbb if necessary
IF(OONF_NEED_PACKETBB)
    TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_rfc5444)
ENDIF(OONF_NEED_PACKETBB)

# link config and common API
TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_config)
TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_common)

# link dlopen() library
TARGET_LINK_LIBRARIES(${OONF_EXE} ${CMAKE_DL_LIBS})

# link extra win32 libs
IF(WIN32)
    TARGET_LINK_LIBRARIES(${OONF_EXE} oonf_regex)

    SET_TARGET_PROPERTIES(${OONF_EXE} PROPERTIES ENABLE_EXPORTS true)
    TARGET_LINK_LIBRARIES(${OONF_EXE} ws2_32 iphlpapi)
ENDIF(WIN32)
