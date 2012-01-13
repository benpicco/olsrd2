SET(OONF_CONFIG_SRCS cfg_cmd.c
                      cfg_db.c
                      cfg_delta.c
                      cfg_io.c
                      cfg_parser.c
                      cfg_schema.c
                      cfg.c)

ADD_LIBRARY(static_config STATIC ${OONF_CONFIG_SRCS})
ADD_LIBRARY(${OONF_LIBPREFIX}_config SHARED ${OONF_CONFIG_SRCS})

IF(WIN32)
    TARGET_LINK_LIBRARIES(static_config ws2_32 iphlpapi)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_config ws2_32 iphlpapi)
ENDIF(WIN32)
