SET(OLSRD_COMMON_SRCS autobuf.c
                      avl_comp.c
                      avl.c
                      daemonize.c
                      netaddr.c
                      string.c
                      template.c)

ADD_LIBRARY(OlsrdCommon ${OLSRD_COMMON_SRCS})
IF(WIN32)
    TARGET_LINK_LIBRARIES(OlsrdCommon ws2_32 iphlpapi)
ENDIF(WIN32)
