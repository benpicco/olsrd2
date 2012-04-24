# add main program
set(OONF_SRCS olsr_main.c 
              olsr_setup.c
              olsr_layer2.c
              olsr_callbacks.c)

# create executable
ADD_EXECUTABLE(${OONF_EXE} ${OONF_SRCS})

# link framework libraries (choose one)
IF (OONF_FRAMEWORD_DYNAMIC)
    include(CMake.link_dynamic)
ELSE (OONF_FRAMEWORD_DYNAMIC)
    include(CMake.link_static)
endif(OONF_FRAMEWORD_DYNAMIC)
