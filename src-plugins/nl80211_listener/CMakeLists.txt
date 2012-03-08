# set library name
SET (libname "nl80211_listener")
SET (source "${CMAKE_CURRENT_SOURCE_DIR}/src/nl80211_listener.c")
SET (linkto nl)

# use generic plugin cmake file
include (../CMake.plugin)
