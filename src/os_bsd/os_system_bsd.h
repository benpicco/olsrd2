/*
 * os_system_bsd.h
 *
 *  Created on: Oct 15, 2011
 *      Author: henning
 */

#ifndef OS_SYSTEM_BSD_H_
#define OS_SYSTEM_BSD_H_

#ifndef OS_NET_SPECIFIC_INCLUDE
#error "DO not include this file directly, always use 'os_system.h'"
#endif

/* BSD os_system runs on "all default" */
#define OS_SYSTEM_GETTIMEOFDAY OS_GENERIC
#define OS_SYSTEM_LOG          OS_GENERIC

#endif /* OS_SYSTEM_BSD_H_ */
