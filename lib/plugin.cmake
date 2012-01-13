# generic plugin cmake-file
ADD_DEFINITIONS(-DPLUGIN_FULLNAME=${libname})

ADD_LIBRARY(static_${libname} STATIC ${source})
ADD_LIBRARY(${OONF_LIBPREFIX}_${libname} SHARED ${source})

IF(WIN32)
    TARGET_LINK_LIBRARIES(${OONF_LIBPREFIX}_${libname} ws2_32 iphlpapi ${OONF_APP})
ENDIF(WIN32)

SET_TARGET_PROPERTIES(${OONF_LIBPREFIX}_${libname} PROPERTIES SOVERSION ${OONF_VERSION})

UNSET(source)
UNSET(libname)
