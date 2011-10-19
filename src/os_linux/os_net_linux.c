/*
 * os_net_linux.c
 *
 *  Created on: Oct 18, 2011
 *      Author: rogge
 */

#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ifaddrs.h>

#include "common/common_types.h"

#include "olsr_cfg.h"
#include "olsr_interface.h"
#include "olsr_logging.h"
#include "olsr.h"
#include "os_net.h"

static int _ioctl_v4, _ioctl_v6;

OLSR_SUBSYSTEM_STATE(_os_net_state);

/**
 * Initialize os_net subsystem
 * @return -1 if an error happened, 0 otherwise
 */
int
os_net_init(void) {
  if (olsr_subsystem_is_initialized(&_os_net_state))
    return 0;

  _ioctl_v4 = socket(AF_INET, SOCK_DGRAM, 0);
  if (_ioctl_v4 == -1) {
    OLSR_WARN(LOG_OS_NET, "Cannot open ipv4 ioctl socket: %s (%d)",
        strerror(errno), errno);
    return -1;
  }

  if (config_global.ipv6) {
    _ioctl_v6 = socket(AF_INET6, SOCK_DGRAM, 0);
    if (_ioctl_v6 == -1) {
      OLSR_WARN(LOG_OS_NET, "Cannot open ipv6 ioctl socket: %s (%d)",
          strerror(errno), errno);
      close(_ioctl_v4);
      return -1;
    }
  }
  else {
    _ioctl_v6 = -1;
  }

  olsr_subsystem_init(&_os_net_state);
  return 0;
}

/**
 * Cleanup os_net subsystem
 */
void
os_net_cleanup(void) {
  if (olsr_subsystem_cleanup(&_os_net_state))
    return;

  if (_ioctl_v4 != -1) {
    close (_ioctl_v4);
    _ioctl_v4 = -1;
  }
  if (_ioctl_v6 != -1) {
    close (_ioctl_v6);
    _ioctl_v6 = -1;
  }
}

/**
 * Updates the data of an interface.
 * @param interf pointer to interface object.
 * @param name name of interface
 * @return -1 if an error happened, 0 otherwise
 */
int
os_net_update_interface(struct olsr_interface_data *data,
    const char *name) {
  struct ifaddrs *ifaddr, *ifa;
  union netaddr_socket *sock;
  struct netaddr addr;
  struct ifreq ifr;

  memset(data, 0, sizeof(*data));

  /* get interface index */
  data->index = if_nametoindex(name);

  if (data->index == 0) {
    return 0;
  }

  if (getifaddrs(&ifaddr) == -1) {
    OLSR_WARN(LOG_OS_NET, "Cannot get interface addresses: %s (%d)",
        strerror(errno), errno);
    return -1;
  }

  for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (strcmp(ifa->ifa_name, name) != 0) {
      continue;
    }

    sock = (union netaddr_socket *)ifa->ifa_addr;

    if (netaddr_from_socket(&addr, sock)) {
      /* just ignore other interfaces */
      continue;
    }

    if (addr.type == AF_INET) {
      memcpy(&data->if_v4, &addr, sizeof(data->if_v4));
    }
    else if (addr.type == AF_INET6) {
      if (IN6_IS_ADDR_LINKLOCAL(addr.addr)) {
        memcpy(&data->linklocal_v6, &addr, sizeof(data->linklocal_v6));
      }
      else if (!(IN6_IS_ADDR_LOOPBACK(addr.addr)
          || IN6_IS_ADDR_MULTICAST(addr.addr)
          || IN6_IS_ADDR_UNSPECIFIED(addr.addr)
          || IN6_IS_ADDR_V4COMPAT(addr.addr)
          || IN6_IS_ADDR_V4MAPPED(addr.addr))) {
        memcpy(&data->if_v6, &addr, sizeof(data->if_v6));
      }
    }
  }

  memset(&ifr, 0, sizeof(ifr));
  strscpy(ifr.ifr_name, name, IFNAMSIZ);

  if (ioctl(_ioctl_v4, SIOCGIFFLAGS, &ifr) < 0) {
    OLSR_WARN(LOG_OS_NET,
        "ioctl SIOCGIFFLAGS (get flags) error on device %s: %s (%d)\n",
        name, strerror(errno), errno);
    return -1;
  }

  if ((ifr.ifr_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP|IFF_RUNNING)) {
    data->up = true;
  }

  return 0;
}
