#!/bin/cmake

# look for git executable 
find_program(found_git git)

message ("GIT app: ${GIT}")

IF(${found_git} STREQUAL "found_git-NOTFOUND" OR NOT EXISTS ${GIT})
    # git executable or repository (.git) is not available
    SET(OONF_SRC_GIT "cannot read git repository")
    SET(OONF_SRC_CHANGE "")
ELSE()
    # everything is fine, read commit and diff stat
    execute_process(COMMAND git describe --long --tags 
        OUTPUT_VARIABLE OONF_SRC_GIT OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND git diff --shortstat HEAD ./src ./src-plugins ./cmake 
        OUTPUT_VARIABLE OONF_SRC_CHANGE OUTPUT_STRIP_TRAILING_WHITESPACE)
ENDIF()

# create builddata file
configure_file (${SRC} ${DST})

# replace spaces with string to convert into list
message ("Generate static plugin loader for '${PLUGINS}'")

string(REPLACE " " ";" PLUGIN_LIST "${PLUGINS}")

# create C file which would call the static plugin constructors 
file(APPEND ${DST} "\n")

FOREACH(plugin ${PLUGIN_LIST})
    file(APPEND ${DST} "extern void hookup_plugin_${plugin}(void);\n")
ENDFOREACH(plugin)

file(APPEND ${DST} "\n")
file(APPEND ${DST} "void\n")
file(APPEND ${DST} "olsr_plugins_load_static(void) {\n")

FOREACH(plugin ${PLUGIN_LIST})
    file(APPEND ${DST} "  hookup_plugin_${plugin}();\n")
ENDFOREACH(plugin)

file(APPEND ${DST} "}\n")
