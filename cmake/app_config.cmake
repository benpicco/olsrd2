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

set (INSTALL_LIB_DIR        lib/olsrv2               CACHE PATH
     "Relative installation directory for libraries")
set (INSTALL_PKGCONFIG_DIR  lib/pkgconfig            CACHE PATH
     "Relative installation directory for pkgconfig file")
set (INSTALL_INCLUDE_DIR    include/olsrv2           CACHE PATH 
     "Relative installation directory for header files")
set (INSTALL_CMAKE_DIR      ${DEF_INSTALL_CMAKE_DIR} CACHE PATH
     "Relative installation directory for CMake files")

###########################################
#### Default Application configuration ####
###########################################

# set name of program the executable and library prefix
set (OONF_APP OLSRd2)
set (OONF_EXE olsrd2)

# set Application library prefix
set (OONF_APP_LIBPREFIX "olsrd2" CACHE STRING 
     "prefix for dynamic and static plugins of the application")

# set default configuration file
set (OONF_DEFAULT_CONF "/etc/${OONF_EXE}/${OONF_EXE}.conf" CACHE FILEPATH
     "Default position of configuration file")

# setup custom text before and after default help message
set (OONF_HELP_PREFIX "OLSRv2 routing agent\\n" CACHE STRING
     "Text to be displayed before command line help")
set (OONF_HELP_SUFFIX "" CACHE STRING
     "Text to be displayed after command line help")

# setup custom text after version string
set (OONF_VERSION_TRAILER "Visit http://www.olsr.org\\\\n" CACHE STRING
     "Text to be displayed after version output")

# set static plugins (list of plugin names, separated by space)
set (OONF_STATIC_PLUGINS "cfgparser_compact cfgio_file ff_etx neighbor_probing" CACHE STRING
     "Space separated list of plugins to compile into application")

# choose if framework should be linked static or dynamic
set (OONF_FRAMEWORD_DYNAMIC false CACHE BOOL
     "Compile the application with dynamic libraries instead of linking everything static")

# set to true to stop application running without root privileges (true/false)
set (OONF_NEED_ROOT true)

# set to true to link packetbb API to application
set (OONF_NEED_PACKETBB true)
