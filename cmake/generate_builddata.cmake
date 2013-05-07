#!/bin/cmake

# look for git executable 
find_program(found_git git)

IF(${found_git} STREQUAL "found_git-NOTFOUND" OR NOT EXISTS ${GIT})
    # git executable or repository (.git) is not available
    SET(OONF_SRC_GIT "cannot read git repository")
    SET(OONF_SRC_CHANGE "")
ELSE()
    # everything is fine, read commit and diff stat
    execute_process(COMMAND git describe --always --long --tags 
        OUTPUT_VARIABLE OONF_SRC_GIT OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND git diff --shortstat HEAD ./src ./src-plugins ./cmake 
        OUTPUT_VARIABLE OONF_SRC_CHANGE OUTPUT_STRIP_TRAILING_WHITESPACE)
ENDIF()

# create builddata file
configure_file (${SRC} ${DST})
