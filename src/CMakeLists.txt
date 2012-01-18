# add static libraries
add_subdirectory(common)
add_subdirectory(config)
add_subdirectory(builddata)

# add source files of core directory
FILE(GLOB SRCS "core/*.c")    
set(OONF_SRCS ${OONF_SRCS} ${SRCS})

# add os_specific source files
IF(WIN32)
    FILE(GLOB SRCS "os_win32/*.c")    
    set(OONF_SRCS ${OONF_SRCS} ${SRCS})
ENDIF(WIN32)

IF(LINUX)
    FILE(GLOB SRCS "os_linux/*.c")    
    set(OONF_SRCS ${OONF_SRCS} ${SRCS})
ENDIF(LINUX)

IF(BSD)
    FILE(GLOB SRCS "os_bsd/*.c")    
    set(OONF_SRCS ${OONF_SRCS} ${SRCS})
ENDIF(BSD)

IF(ANDROID)
    include_directories(src/android)
    FILE(GLOB SRCS "android/*.c")    
    set(OONF_SRCS ${OONF_SRCS} ${SRCS})
ENDIF(ANDROID)

# add main program
set(OONF_SRCS ${OONF_SRCS} olsr_main.c olsr_setup.c)

# activate symbol export for win32
IF(WIN32)
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-all-symbols")
ENDIF(WIN32)

# create executable
ADD_EXECUTABLE(${OONF_EXE} ${OONF_SRCS})

# the order of static libraries is important
# earlier libraries can use the functions of later, not the
# other way around

# link plugin loader
TARGET_LINK_LIBRARIES(${OONF_EXE} static_pluginloader)

# link static plugins
FOREACH(plugin ${OONF_STATIC_PLUGINS})
    TARGET_LINK_LIBRARIES(${OONF_EXE} static_${plugin})
ENDFOREACH(plugin)

TARGET_LINK_LIBRARIES(${OONF_EXE} static_config)
TARGET_LINK_LIBRARIES(${OONF_EXE} static_common)

# link builddata
TARGET_LINK_LIBRARIES(${OONF_EXE} static_builddata)

# link dlopen() library
TARGET_LINK_LIBRARIES(${OONF_EXE} ${CMAKE_DL_LIBS})

# link extra win32 libs
IF(WIN32)
    SET_TARGET_PROPERTIES(${OONF_EXE} PROPERTIES ENABLE_EXPORTS true)
    TARGET_LINK_LIBRARIES(${OONF_EXE} ws2_32 iphlpapi)
ENDIF(WIN32)
