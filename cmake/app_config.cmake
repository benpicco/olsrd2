# calculate default cmake file install target
if (WIN32 AND NOT CYGWIN)
  set(DEF_INSTALL_CMAKE_DIR CMake)
else ()
  set(DEF_INSTALL_CMAKE_DIR lib/CMake)
endif ()

# set to debug build if variable not set
IF (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE debug)
ENDIF (NOT CMAKE_BUILD_TYPE)

###########################
#### API configuration ####
###########################

# set CMAKE build type for api and plugins
# (Debug, Release, MinSizeRel)
set (CMAKE_BUILD_TYPE Debug CACHE STRING
     "Choose the type of build (Debug Release RelWithDebInfo MinSizeRel)")

# maximum logging level
set (OONF_LOGGING_LEVEL debug CACHE STRING 
    "Maximum logging level compiled into OONF application (none, warn, info, debug)")

# remove help texts from application, core-api and plugins
set (OONF_REMOVE_HELPTEXT false CACHE BOOL
     "Set if you want to remove the help texts from application to reduce size")

######################################
#### Install target configuration ####
######################################

set (INSTALL_LIB_DIR        lib/oonf                 CACHE PATH
     "Relative installation directory for libraries")
set (INSTALL_PKGCONFIG_DIR  lib/pkgconfig            CACHE PATH
     "Relative installation directory for pkgconfig file")
set (INSTALL_INCLUDE_DIR    include/oonf             CACHE PATH 
     "Relative installation directory for header files")
set (INSTALL_CMAKE_DIR      ${DEF_INSTALL_CMAKE_DIR} CACHE PATH
     "Relative installation directory for CMake files")

###########################################
#### Default Application configuration ####
###########################################

# set name of program the executable and library prefix
set (OONF_APP DLEP CACHE STRING
     "Name of the Application (to be displayed in output)")
set (OONF_EXE dlep CACHE STRING
     "Name of the executable file for the application")

# set Application library prefix
set (OONF_APP_LIBPREFIX dlep CACHE STRING
     "Application specific prefix for plugins (libXXXX_...)")

# set default configuration file
set (OONF_DEFAULT_CONF "/etc/${OONF_EXE}.conf" CACHE FILEPATH
     "Default position of configuration file")

# setup custom text before and after default help message
set (OONF_HELP_PREFIX "DLEP layer-2 information management daemon\\n" CACHE STRING
     "Text to be displayed before command line help")
set (OONF_HELP_SUFFIX "" CACHE STRING
     "Text to be displayed after command line help")

# setup custom text after version string
set (OONF_VERSION_TRAILER "Visit http://www.olsr.org\\\\n" CACHE STRING
     "Text to be displayed after version output")

# set application version (e.g. 0.7.0)
#set (OONF_APP_VERSION 0.7.0

# set static plugins (list of plugin names, separated by space)
set (OONF_STATIC_PLUGINS "cfgparser_compact cfgio_file" CACHE STRING
     "Space separated list of plugins to compile into application")

# choose if framework should be linked static or dynamic
# TODO: dynamic framework still has problems
set (OONF_FRAMEWORD_DYNAMIC false)

# set to true to stop application running without root privileges (true/false)
set (OONF_NEED_ROOT false CACHE BOOL
     "True if the application needs root rights")

# set to true if the application needs to set ip routes for traffic forwarding
set (OONF_NEED_ROUTING false CACHE BOOL
     "True if the application needs to modify the routing tables")

# set to true to link packetbb API to application
set (OONF_NEED_PACKETBB true CACHE BOOL
     "True if the application needs the packetbb API")

# name of the libnl library
set (OONF_LIBNL nl CACHE STRING
     "Name of the libnl library (might be nl-tiny for Openwrt)")
