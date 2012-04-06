# detect operation system and add missing variables for easy CMAKE OS detection
STRING(TOLOWER ${CMAKE_SYSTEM_NAME} SYSTEM_NAME)

IF (UNIX)
    ADD_DEFINITIONS(-DOS_UNIX)
ENDIF (UNIX)

IF (${SYSTEM_NAME} MATCHES "linux")
    message("Linux detected")
    SET(LINUX true)
ENDIF (${SYSTEM_NAME} MATCHES "linux")

IF (${SYSTEM_NAME} MATCHES "android" OR ANDROID)
    message("Android detected")
    SET(LINUX true)
    SET(ANDROID true)
    SET(UNIX true)
ENDIF (${SYSTEM_NAME} MATCHES "android" OR ANDROID)

IF (${SYSTEM_NAME} MATCHES "bsd")
    message("BSD detected")
    SET(BSD true)
ENDIF (${SYSTEM_NAME} MATCHES "bsd")

IF (APPLE)
    message("Mac OS detected")
    set(BSD true)
ENDIF (APPLE)

IF (WIN32)
    message("Win32 detected")
ENDIF (WIN32)
