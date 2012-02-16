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

#include <linux/netlink.h>

#include "common/netaddr.h"
#include "os_helper.h"

/* Linux os_system runs on "all default" except for init/cleanup */
#define OS_SYSTEM_INIT         OS_SPECIFIC
#define OS_SYSTEM_INIT_IF      OS_SPECIFIC
#define OS_SYSTEM_SET_IFSTATE  OS_SPECIFIC
#define OS_SYSTEM_GETTIMEOFDAY OS_GENERIC
#define OS_SYSTEM_LOG          OS_GENERIC

int os_system_netlink_addreq(struct nlmsghdr *n,
    int type, const void *data, int len);
int os_system_netlink_sync_send(struct nlmsghdr *nl_hdr);
int os_system_netlink_async_send(struct nlmsghdr *nl_hdr);

static INLINE int
os_system_netlink_addnetaddr(struct nlmsghdr *n,
    int type, const struct netaddr *addr) {
  return os_system_netlink_addreq(n, type, addr->addr, netaddr_get_maxprefix(addr)/8);
}


#endif /* OS_SYSTEM_LINUX_H_ */
