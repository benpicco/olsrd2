SET(OONF_COMMON_SRCS autobuf.c
                      avl_comp.c
                      avl.c
                      daemonize.c
                      netaddr.c
                      string.c
                      template.c)

ADD_LIBRARY(static_common ${OONF_COMMON_SRCS})
ADD_LIBRARY(${OONF_LIBPREFIX}_common SHARED ${OONF_COMMON_SRCS})

IF(WIN32)
    TARGET_LINK_LIBRARIES(static_common ws2_32 iphlpapi)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_common ws2_32 iphlpapi)
ENDIF(WIN32)