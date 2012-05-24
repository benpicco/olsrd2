# add main program
set(OONF_SRCS olsr_main.c 
              olsr_setup.c
              )

# create executable
ADD_EXECUTABLE(${OONF_EXE} ${OONF_SRCS})

# add path to install target
INSTALL (TARGETS ${OONF_EXE} DESTINATION bin)

# link framework libraries (choose one)
IF (OONF_FRAMEWORD_DYNAMIC)
    include(CMake.link_dynamic)
ELSE (OONF_FRAMEWORD_DYNAMIC)
    include(CMake.link_static)
endif(OONF_FRAMEWORD_DYNAMIC)
