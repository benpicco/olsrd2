# add static libraries
add_subdirectory(common)
add_subdirectory(config)
add_subdirectory(builddata)

# add source files of core directory
FILE(GLOB SRCS "core/*.c")    
set(OLSRD_SRCS ${OLSRD_SRCS} ${SRCS})

# add os_specific source files
IF(WIN32)
    FILE(GLOB SRCS "win32/*.c")    
    set(OLSRD_SRCS ${OLSRD_SRCS} ${SRCS})
ENDIF(WIN32)

IF(UNIX)
    FILE(GLOB SRCS "unix/*.c")    
    set(OLSRD_SRCS ${OLSRD_SRCS} ${SRCS})
ENDIF(UNIX)

IF(LINUX)
    FILE(GLOB SRCS "linux/*.c")    
    set(OLSRD_SRCS ${OLSRD_SRCS} ${SRCS})
ENDIF(LINUX)

IF(BSD)
    FILE(GLOB SRCS "bsd/*.c")    
    set(OLSRD_SRCS ${OLSRD_SRCS} ${SRCS})
ENDIF(BSD)

# activate symbol export for win32
IF(WIN32)
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-all-symbols")
ENDIF(WIN32)

# create executable
ADD_EXECUTABLE(olsrd ${OLSRD_SRCS})
TARGET_LINK_LIBRARIES(olsrd OlsrdCommon OlsrdConfig OlsrdBuildData)
TARGET_LINK_LIBRARIES(olsrd ${CMAKE_DL_LIBS})

# link extra win32 libs
IF(WIN32)
    SET_TARGET_PROPERTIES(olsrd PROPERTIES ENABLE_EXPORTS true)
    TARGET_LINK_LIBRARIES(olsrd ws2_32 iphlpapi)
ENDIF(WIN32)

# link static plugins
FOREACH(plugin ${OLSRD_STATIC_PLUGINS})
    TARGET_LINK_LIBRARIES(olsrd static_${plugin})
ENDFOREACH(plugin)
