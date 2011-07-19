# detect operation system and add compiler hints
STRING(TOLOWER ${CMAKE_SYSTEM_NAME} SYSTEM_NAME)

IF (UNIX)
    ADD_DEFINITIONS(-DOS_UNIX)
ENDIF (UNIX)

IF (${SYSTEM_NAME} MATCHES "linux")
    message("Linux detected")
    ADD_DEFINITIONS(-DOS_LINUX)
    SET(LINUX true)
ENDIF (${SYSTEM_NAME} MATCHES "linux")

IF (${SYSTEM_NAME} MATCHES "android" OR ANDROID)
    message("Android detected")
    ADD_DEFINITIONS(-DOS_ANDROID -DOS_LINUX)
    SET(LINUX true)
    SET(ANDROID true)
    SET(UNIX true)
ENDIF (${SYSTEM_NAME} MATCHES "android" OR ANDROID)

IF (${SYSTEM_NAME} MATCHES "bsd")
    message("BSD detected")
    ADD_DEFINITIONS(-DOS_BSD)
    SET(BSD true)
ENDIF (${SYSTEM_NAME} MATCHES "bsd")

IF (APPLE)
    message("Mac OS detected")
    ADD_DEFINITIONS(-DOS_APPLE -DOS_BSD)
    set(BSD true)
ENDIF (APPLE)

IF (WIN32)
    message("Win32 detected")
    ADD_DEFINITIONS(-DOS_WIN32)
ENDIF (WIN32)
