# build framework
add_subdirectory(builddata)
add_subdirectory(common)
IF (WIN32 OR ANDROID)
    add_subdirectory(regex)
ENDIF(WIN32 OR ANDROID)
add_subdirectory(config)
add_subdirectory(packetbb)
add_subdirectory(core)
