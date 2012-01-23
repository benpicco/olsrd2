# build framework
add_subdirectory(common)
add_subdirectory(config)
add_subdirectory(builddata)
add_subdirectory(core)

# add main program
set(OONF_SRCS ${OONF_SRCS} olsr_main.c olsr_setup.c)

# create executable
ADD_EXECUTABLE(${OONF_EXE} ${OONF_SRCS})

# link framework libraries (choose one)
IF (OONF_FRAMEWORD_DYNAMIC)
    include(CMake.link_dynamic)
ELSE (OONF_FRAMEWORD_DYNAMIC)
    include(CMake.link_static)
endif(OONF_FRAMEWORD_DYNAMIC)
