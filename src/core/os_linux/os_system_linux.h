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
#include "olsr_socket.h"
#include "os_helper.h"

/* Linux os_system runs on "all default" except for init/cleanup */
#define OS_SYSTEM_INIT         OS_SPECIFIC
#define OS_SYSTEM_INIT_IF      OS_SPECIFIC
#define OS_SYSTEM_SET_IFSTATE  OS_SPECIFIC
#define OS_SYSTEM_GETTIMEOFDAY OS_GENERIC
#define OS_SYSTEM_LOG          OS_GENERIC

struct os_system_netlink {
  struct olsr_socket_entry socket;
  struct autobuf out;
  struct nlmsghdr *in;

  uint32_t seq_used;
  uint32_t seq_sent;

  int msg_in_transit;

  void (*cb_message)(struct nlmsghdr *hdr);
  void (*cb_error)(uint32_t seq, int error);
  void (*cb_timeout)(void);
  void (*cb_done)(uint32_t seq);

  struct olsr_timer_entry timeout;
};

EXPORT int os_system_netlink_add(struct os_system_netlink *,
    int protocol, uint32_t multicast);
EXPORT void os_system_netlink_remove(struct os_system_netlink *);
EXPORT int os_system_netlink_send(struct os_system_netlink *fd,
    struct nlmsghdr *nl_hdr);

EXPORT int os_system_netlink_addreq(struct nlmsghdr *n,
    int type, const void *data, int len);

static INLINE int
os_system_netlink_addnetaddr(struct nlmsghdr *n,
    int type, const struct netaddr *addr) {
  return os_system_netlink_addreq(n, type, addr->addr, netaddr_get_maxprefix(addr)/8);
}


#endif /* OS_SYSTEM_LINUX_H_ */
