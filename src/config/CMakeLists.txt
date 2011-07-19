SET(OLSRD_CONFIG_SRCS io/cfg_io_file.c
#                      parser/cfg_parser_compact.c
                      cfg_cmd.c
                      cfg_db.c
                      cfg_delta.c
                      cfg_io.c
                      cfg_memory.c
                      cfg_parser.c
                      cfg_schema.c
                      cfg.c)

ADD_LIBRARY(OlsrdConfig STATIC ${OLSRD_CONFIG_SRCS})
IF(WIN32)
    TARGET_LINK_LIBRARIES(OlsrdConfig ws2_32 iphlpapi)
ENDIF(WIN32)
