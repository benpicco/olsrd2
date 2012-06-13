# the order of static libraries is important
# earlier libraries can use the functions of later, not the
# other way around

# link static plugins
FOREACH(plugin ${OONF_STATIC_PLUGINS})
    IF(TARGET oonf_static_${plugin})
        TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive oonf_static_${plugin} -Wl,--no-whole-archive)
    ELSE (TARGET oonf_static_${plugin})
        TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive ${OONF_APP_LIBPREFIX}_static_${plugin} -Wl,--no-whole-archive)
    ENDIF(TARGET oonf_static_${plugin})
ENDFOREACH(plugin)

# link core
TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive oonf_static_core -Wl,--no-whole-archive)

# link packetbb if necessary
IF(OONF_NEED_PACKETBB)
    TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive oonf_static_packetbb -Wl,--no-whole-archive)
ENDIF(OONF_NEED_PACKETBB)

# link config and common API
TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive oonf_static_config -Wl,--no-whole-archive)
TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive oonf_static_common -Wl,--no-whole-archive)

# link dlopen() library
TARGET_LINK_LIBRARIES(${OONF_EXE} ${CMAKE_DL_LIBS})

# link extra win32 libs
IF(WIN32)
    TARGET_LINK_LIBRARIES(${OONF_EXE} -Wl,--whole-archive static_regex -Wl,--no-whole-archive)

    SET_TARGET_PROPERTIES(${OONF_EXE} PROPERTIES ENABLE_EXPORTS true)
    TARGET_LINK_LIBRARIES(${OONF_EXE} ws2_32 iphlpapi)
ENDIF(WIN32)
