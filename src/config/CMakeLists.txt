SET(OLSRD_CONFIG_SRCS cfg_cmd.c
                      cfg_db.c
                      cfg_delta.c
                      cfg_io.c
                      cfg_memory.c
                      cfg_parser.c
                      cfg_schema.c
                      cfg.c)

ADD_LIBRARY(static_config STATIC ${OLSRD_CONFIG_SRCS})
ADD_LIBRARY(olsrd_config SHARED ${OLSRD_CONFIG_SRCS})

IF(WIN32)
    TARGET_LINK_LIBRARIES(static_config ws2_32 iphlpapi)
    TARGET_LINK_LIBRARIES(olsrd_config ws2_32 iphlpapi)
ENDIF(WIN32)
