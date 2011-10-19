/*
 * os_system_linux.h
 *
 *  Created on: Oct 15, 2011
 *      Author: henning
 */

#ifndef OS_SYSTEM_LINUX_H_
#define OS_SYSTEM_LINUX_H_

#ifndef OS_NET_SPECIFIC_INCLUDE
#error "DO not include this file directly, always use 'os_system.h'"
#endif

#include "os_helper.h"

/* Linux os_system runs on "all default" except for init/cleanup */
#define OS_SYSTEM_INIT         OS_SPECIFIC
#define OS_SYSTEM_GETTIMEOFDAY OS_GENERIC
#define OS_SYSTEM_LOG          OS_GENERIC

#endif /* OS_SYSTEM_LINUX_H_ */
