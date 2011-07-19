# generic plugin cmake-file
ADD_DEFINITIONS(-DPLUGIN_FULLNAME=${libname})

ADD_LIBRARY(static_${libname} STATIC ${source})
ADD_LIBRARY(olsrd_${libname} SHARED ${source})

IF(WIN32)
    TARGET_LINK_LIBRARIES(olsrd_${libname} ws2_32 iphlpapi olsrd)
ENDIF(WIN32)

SET_TARGET_PROPERTIES(olsrd_${libname} PROPERTIES SOVERSION ${OLSRD_VERSION})

UNSET(source)
UNSET(libname)
