#!/bin/cmake

# look for git executable 
find_program(found_git git)

IF(${found_git} STREQUAL "found_git-NOTFOUND" OR NOT EXISTS ${GIT})
    # git executable or repository (.git) is not available
    SET(OLSRD_SRC_GIT "cannot read git repository")
    SET(OLSRD_SRC_CHANGE "")
ELSE()
    # everything is fine, read commit and diff stat
    execute_process(COMMAND git describe --tags 
        OUTPUT_VARIABLE OLSRD_SRC_GIT OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND git diff --shortstat HEAD ./src/ ./lib 
        OUTPUT_VARIABLE OLSRD_SRC_CHANGE OUTPUT_STRIP_TRAILING_WHITESPACE)
ENDIF()

# create builddata file
configure_file (${SRC} ${DST})
