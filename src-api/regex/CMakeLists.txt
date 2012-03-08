SET(OONF_REGEX_SRCS regcomp.c
                    regerror.c
                    regexec.c
                    regfree.c)

ADD_LIBRARY(static_regex ${OONF_REGEX_SRCS})
ADD_LIBRARY(${OONF_LIBPREFIX}_regex SHARED ${OONF_REGEX_SRCS})

IF(WIN32)
    TARGET_LINK_LIBRARIES(static_regex static_common)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_regex ${OONF_LIBPREFIX}_common)

    TARGET_LINK_LIBRARIES(static_regex ws2_32 iphlpapi)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_regex ws2_32 iphlpapi)
ENDIF(WIN32)
