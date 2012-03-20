# prepare packetbb API configuration
configure_file (${CMAKE_CURRENT_SOURCE_DIR}/pbb_api_config.h.in ${CMAKE_BINARY_DIR}/packetbb/pbb_api_config.h)

SET(OLSRD_PACKETBB_SRCS pbb_context.c
                        pbb_msg_generator.c
                        pbb_pkt_generator.c
                        pbb_reader.c
                        pbb_tlv_writer.c
                        pbb_writer.c)

ADD_LIBRARY(static_packetbb STATIC ${OLSRD_PACKETBB_SRCS})
ADD_LIBRARY(${OONF_LIBPREFIX}_packetbb SHARED ${OLSRD_PACKETBB_SRCS})

IF(WIN32)
    TARGET_LINK_LIBRARIES(static_packetbb static_common)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_packetbb ${OONF_LIBPREFIX}_common)

    TARGET_LINK_LIBRARIES(static_packetbb ws2_32 iphlpapi)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_packetbb ws2_32 iphlpapi)
ENDIF(WIN32)
