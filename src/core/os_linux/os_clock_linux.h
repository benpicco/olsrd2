/*
 * os_system_linux.h
 *
 *  Created on: Oct 15, 2011
 *      Author: henning
 */

#ifndef OS_CLOCK_LINUX_H_
#define OS_CLOCK_LINUX_H_

#ifndef OS_NET_SPECIFIC_INCLUDE
#error "DO not include this file directly, always use 'os_system.h'"
#endif

#include "os_helper.h"

/* Linux os_system runs on "all default" except for init/cleanup */
#define OS_CLOCK_GETTIMEOFDAY  OS_GENERIC
#define OS_CLOCK_INIT          OS_SPECIFIC

#endif /* OS_CLOCK_LINUX_H_ */
