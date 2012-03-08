# build framework
add_subdirectory(common)
add_subdirectory(config)
add_subdirectory(builddata)
add_subdirectory(core)
IF (WIN32 OR ANDROID)
    add_subdirectory(regex)
ENDIF(WIN32 OR ANDROID)
