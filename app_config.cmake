###############################
#### Generic configuration ####
###############################

# set CMAKE build type for application, api and plugins
# (Debug, Release, MinSizeRel)
set (CMAKE_BUILD_TYPE Debug)

# keep logging level for application, core-api and plugins down to level
# (none, warn, info, debug)
set (OONF_LOGGING_LEVEL debug)

# remove help texts from application, core-api and plugins
set (OONF_REMOVE_HELPTEXT false)

###########################################
#### Default Application configuration ####
###########################################

# set name of program the executable and library prefix
set (OONF_APP Olsrd)
set (OONF_EXE olsrd)

# set Application library prefix
set (OONF_APP_LIBPREFIX olsr)

# set default configuration file
set (OONF_DEFAULT_CONF "/etc/${OONF_EXE}.conf")

# setup custom text before and after default help message
set (OONF_HELP_PREFIX "Activates OLSR.org routing daemon\\n")
set (OONF_HELP_SUFFIX "")

# setup custom text after version string
set (OONF_VERSION_TRAILER "Visit http://www.olsr.org\\\\n")

# set application version (e.g. 0.7.0)
set (OONF_APP_VERSION 0.7.0)

# set static plugins (list of plugin names, separated by space/newline)
set (OONF_STATIC_PLUGINS cfgparser_compact cfgio_file remotecontrol httptelnet)

# choose if framework should be linked static or dynamic
set (OONF_FRAMEWORD_DYNAMIC false)

# set to true to stop application running without root privileges (true/false)
set (OONF_NEED_ROOT false)

# set to true if the application needs to set ip routes for traffic forwarding
set (OONF_NEED_ROUTING false)

# set to true to link packetbb API to application
set (OONF_NEED_PACKETBB true)

# name of the libnl library
set (OONF_LIBNL nl)
