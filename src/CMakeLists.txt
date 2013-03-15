# create application data for including
include (../cmake/prepare_app.cmake)

# add main program
set(OONF_SRCS olsr_main.c 
              olsr_setup.c
              ${PROJECT_BINARY_DIR}/app_data.c
              
              nhdp/nhdp.c
              nhdp/nhdp_db.c
              nhdp/nhdp_hysteresis.c
              nhdp/nhdp_interfaces.c
              nhdp/nhdp_metric.c
              nhdp/nhdp_mpr.c
              nhdp/nhdp_reader.c
              nhdp/nhdp_writer.c
              
              olsrv2/olsrv2.c
              olsrv2/olsrv2_lan.c
              olsrv2/olsrv2_originator_set.c
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
