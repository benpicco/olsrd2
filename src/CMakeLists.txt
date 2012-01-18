# build framework
include(CMake.build)

# add main program
set(OONF_SRCS ${OONF_SRCS} olsr_main.c olsr_setup.c)

# create executable
ADD_EXECUTABLE(${OONF_EXE} ${OONF_SRCS})

# link framework libraries
include(CMake.link)
