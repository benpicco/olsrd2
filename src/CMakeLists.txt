# create application data for including
include (../cmake/prepare_app.cmake)

# add main program
set(OONF_SRCS olsr_main.c 
              olsr_setup.c
              olsr_layer2.c
              ${PROJECT_BINARY_DIR}/app_data.c
              )

# create executable
ADD_EXECUTABLE(${OONF_EXE} ${OONF_SRCS})

# add path to install target
INSTALL (TARGETS ${OONF_EXE} DESTINATION bin)

# link framework libraries (choose one)
IF (OONF_FRAMEWORD_DYNAMIC)
    include(../cmake/link_app_dynamic.cmake)
ELSE (OONF_FRAMEWORD_DYNAMIC)
    include(../cmake/link_app_static.cmake)
endif(OONF_FRAMEWORD_DYNAMIC)
