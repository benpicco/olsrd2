SET(OONF_CORE_SRCS olsr_cfg.c
                   olsr_clock.c
                   olsr_http.c
                   olsr_interface.c
                   olsr_logging_cfg.c
                   olsr_logging.c
                   olsr_memcookie.c
                   olsr_netaddr_acl.c
                   olsr_packet_socket.c
                   olsr_plugins.c
                   olsr_socket.c
                   olsr_stream_socket.c
                   olsr_telnet.c
                   olsr_timer.c
                   olsr.c
                   os_net_generic.c
                   os_system_generic.c)
                   
# add os_specific source files
IF(WIN32)
    FILE(GLOB SRCS "os_win32/*.c")    
    set(OONF_SRCS ${OONF_SRCS} ${SRCS})
ENDIF(WIN32)

IF(LINUX)
    SET(OONF_CORE_SRCS ${OONF_CORE_SRCS}
                       os_linux/os_net_linux.c
                       os_linux/os_system_linux.c
                       os_linux/os_routing_linux.c
                       os_linux/os_clock_linux.c)
ENDIF(LINUX)

IF(BSD)
    FILE(GLOB SRCS "os_bsd/*.c")    
    set(OONF_SRCS ${OONF_SRCS} ${SRCS})
ENDIF(BSD)

IF(ANDROID)
    include_directories(src/android)
    FILE(GLOB SRCS "android/*.c")    
    set(OONF_SRCS ${OONF_SRCS} ${SRCS})
ENDIF(ANDROID)

ADD_LIBRARY(static_core STATIC ${OONF_CORE_SRCS})
ADD_LIBRARY(${OONF_LIBPREFIX}_core SHARED ${OONF_CORE_SRCS})

TARGET_LINK_LIBRARIES(static_core rt)
TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_core rt)

IF(WIN32)
    TARGET_LINK_LIBRARIES(static_core static_common)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_core ${OONF_LIBPREFIX}_common)

    TARGET_LINK_LIBRARIES(static_config static_regex)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_config ${OONF_LIBPREFIX}_regex)

    TARGET_LINK_LIBRARIES(static_core static_config)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_core ${OONF_LIBPREFIX}_config)

    TARGET_LINK_LIBRARIES(static_core ws2_32 iphlpapi)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_core ws2_32 iphlpapi)
ENDIF(WIN32)
