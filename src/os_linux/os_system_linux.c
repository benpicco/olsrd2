/*
 * os_netlink.c
 *
 *  Created on: Oct 19, 2011
 *      Author: rogge
 */

/* must be first because of a problem with linux/rtnetlink.h */
#include <sys/socket.h>

/* and now the rest of the includes */
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "common/common_types.h"
#include "common/string.h"
#include "olsr_interface.h"
#include "olsr_socket.h"
#include "olsr.h"
#include "os_system.h"

/* buffer for reading netlink messages */
#define NETLINK_BUFFER_SIZE 4096

/* ip forwarding */
#define PROC_IPFORWARD_V4 "/proc/sys/net/ipv4/ip_forward"
#define PROC_IPFORWARD_V6 "/proc/sys/net/ipv6/conf/all/forwarding"

/* Redirect proc entry */
#define PROC_IF_REDIRECT "/proc/sys/net/ipv4/conf/%s/send_redirects"
#define PROC_ALL_REDIRECT "/proc/sys/net/ipv4/conf/all/send_redirects"

/* IP spoof proc entry */
#define PROC_IF_SPOOF "/proc/sys/net/ipv4/conf/%s/rp_filter"
#define PROC_ALL_SPOOF "/proc/sys/net/ipv4/conf/all/rp_filter"

static int _writeToProc(const char *file, char *old, char value);
static bool _is_at_least_linuxkernel_2_6_31(void);
static void _netlink_handler(int fd, void *data,
    bool event_read, bool event_write);
static void _handle_nl_link(void);
static void _handle_nl_addr(void);

/* global procfile state before initialization */
static char _original_rp_filter;
static char _original_icmp_redirect;

/* rtnetlink socket */
static int _rtnetlink_fd = -1;
static struct olsr_socket_entry _rtnetlink_socket = {
  .process = _netlink_handler,
  .event_read = true,
};

/* ioctl socket */
static int _ioctl_fd = -1;

/* static buffers for reading a netlink message */
static struct iovec _netlink_iov;
static struct sockaddr_nl _netlink_nladdr;
static struct msghdr _netlink_msg = {
  &_netlink_nladdr,
  sizeof(_netlink_nladdr),
  &_netlink_iov,
  1,
  NULL,
  0,
  0
};

struct nlmsghdr *_netlink_header;

OLSR_SUBSYSTEM_STATE(_os_system_state);

/**
 * Initialize os-specific subsystem
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_init(void) {
  struct sockaddr_nl addr;

  if (olsr_subsystem_is_initialized(&_os_system_state)) {
    return 0;
  }

  _ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (_ioctl_fd == -1) {
    OLSR_WARN(LOG_OS_SYSTEM, "Cannot open ioctl socket: %s (%d)",
        strerror(errno), errno);
    return -1;
  }
  _netlink_header = calloc(NETLINK_BUFFER_SIZE, 1);
  if (_netlink_header == NULL) {
    OLSR_WARN_OOM(LOG_OS_SYSTEM);
    close(_ioctl_fd);
    return -1;
  }
  _netlink_iov.iov_base = _netlink_header;
  _netlink_iov.iov_len = NETLINK_BUFFER_SIZE;

  _rtnetlink_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (_rtnetlink_fd < 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Cannot open rtnetlink socket: %s (%d)",
        strerror(errno), errno);
    free(_netlink_header);
    close(_ioctl_fd);
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

  /* kernel will assign appropiate number instead of pid */
  addr.nl_pid = 0;

  if (bind(_rtnetlink_fd, (struct sockaddr *)&addr, sizeof(addr))<0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not bind rtnetlink socket: %s (%d)",
        strerror(errno), errno);
    close (_rtnetlink_fd);
    free(_netlink_header);
    close(_ioctl_fd);
    return -1;
  }

  /* add socket listener */
  _rtnetlink_socket.fd = _rtnetlink_fd;
  olsr_socket_add(&_rtnetlink_socket);

  /* mark both flags non-used */
  _original_icmp_redirect = 0;
  _original_rp_filter = 0;

  if (_writeToProc(PROC_ALL_REDIRECT, &_original_icmp_redirect, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable ICMP redirects! "
        "You should manually ensure that ICMP redirects are disabled!");
  }

  /* check kernel version and disable global rp_filter */
  if (_is_at_least_linuxkernel_2_6_31()) {
    if (_writeToProc(PROC_ALL_SPOOF, &_original_rp_filter, '0')) {
      OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable global rp_filter "
          "(necessary for kernel 2.6.31 and newer)! You should manually "
          "ensure that rp_filter is disabled!");
    }
  }

  olsr_subsystem_init(&_os_system_state);
  return 0;
}

/**
 * Cleanup os-specific subsystem
 */
void
os_system_cleanup(void) {
  if (olsr_subsystem_cleanup(&_os_system_state))
    return;

  if (_original_icmp_redirect != 0
      && _writeToProc(PROC_ALL_REDIRECT, &_original_icmp_redirect, '0') != 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "WARNING! Could not restore ICMP redirect flag %s to %c!",
        PROC_ALL_REDIRECT, _original_icmp_redirect);
  }

  /* check kernel version and disable global rp_filter */
  if (_writeToProc(PROC_ALL_SPOOF, &_original_rp_filter, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "WARNING! Could not restore global rp_filter flag %s to %c!",
        PROC_ALL_SPOOF, _original_rp_filter);
  }

  close(_rtnetlink_fd);
  close(_ioctl_fd);
}

/**
 * Initialize interface for mesh usage
 * @param interf pointer to interface object
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_init_mesh_if(struct olsr_interface *interf) {
  char procfile[FILENAME_MAX];
  char old_redirect = 0, old_spoof = 0;

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_REDIRECT, interf->name);

  if (_writeToProc(procfile, &old_redirect, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable ICMP redirects! "
        "You should manually ensure that ICMP redirects are disabled!");
  }

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_SPOOF, interf->name);

  if (_writeToProc(procfile, &old_spoof, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable the IP spoof filter! "
        "You should mannually ensure that IP spoof filtering is disabled!");
  }

  interf->_original_state = (old_redirect << 8) | (old_spoof);
  return 0;
}

/**
 * Cleanup interface after mesh usage
 * @param interf pointer to interface object
 */
void
os_system_cleanup_mesh_if(struct olsr_interface *interf) {
  char restore_redirect, restore_spoof;
  char procfile[FILENAME_MAX];

  restore_redirect = (interf->_original_state >> 8) & 255;
  restore_spoof = (interf->_original_state & 255);

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_REDIRECT, interf->name);

  if (restore_redirect != 0
      && _writeToProc(procfile, NULL, restore_redirect) != 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not restore ICMP redirect flag %s to %c",
        procfile, restore_redirect);
  }

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_SPOOF, interf->name);

  if (restore_spoof != 0
      && _writeToProc(procfile, NULL, restore_spoof) != 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not restore IP spoof flag %s to %c",
        procfile, restore_spoof);
  }

  interf->_original_state = 0;
  return;
}

/**
 * Set interface up or down
 * @param dev pointer to name of interface
 * @param up true if interface should be up, false if down
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_set_interface_state(const char *dev, bool up) {
  int oldflags;
  struct ifreq ifr;

  memset(&ifr, 0, sizeof(ifr));
  strscpy(ifr.ifr_name, dev, IFNAMSIZ);

  if (ioctl(_ioctl_fd, SIOCGIFFLAGS, &ifr) < 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "ioctl SIOCGIFFLAGS (get flags) error on device %s: %s (%d)\n",
        dev, strerror(errno), errno);
    return -1;
  }

  oldflags = ifr.ifr_flags;
  if (up) {
    ifr.ifr_flags |= IFF_UP;
  }
  else {
    ifr.ifr_flags &= ~IFF_UP;
  }

  if (oldflags == ifr.ifr_flags) {
    /* interface is already up/down */
    return 0;
  }

  if (ioctl(_ioctl_fd, SIOCSIFFLAGS, &ifr) < 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "ioctl SIOCSIFFLAGS (set flags %s) error on device %s: %s (%d)\n",
        up ? "up" : "down", dev, strerror(errno), errno);
    return -1;
  }
  return 0;
}

/**
 * Overwrite a numeric entry in the procfile system and keep the old
 * value.
 * @param file pointer to filename (including full path)
 * @param old pointer to memory to store old value
 * @param value new value
 * @return -1 if an error happened, 0 otherwise
 */
static int
_writeToProc(const char *file, char *old, char value) {
  int fd;
  char rv;

  if ((fd = open(file, O_RDWR)) < 0) {
    goto writetoproc_error;
  }

  if (read(fd, &rv, 1) != 1) {
    goto writetoproc_error;
  }

  if (rv != value) {
    if (lseek(fd, SEEK_SET, 0) == -1) {
      goto writetoproc_error;
    }

    if (write(fd, &value, 1) != 1) {
      goto writetoproc_error;
    }

    OLSR_DEBUG(LOG_OS_SYSTEM, "Writing '%c' (was %c) to %s", value, rv, file);
  }

  if (close(fd) != 0) {
    goto writetoproc_error;
  }

  if (old && rv != value) {
    *old = rv;
  }

  return 0;

writetoproc_error:
  OLSR_WARN(LOG_OS_SYSTEM,
    "Error, cannot read proc entry %s: %s (%d)\n",
    file, strerror(errno), errno);
  return -1;
}

static bool
_is_at_least_linuxkernel_2_6_31(void) {
  struct utsname uts;
  char *next;
  int first = 0, second = 0, third = 0;

  memset(&uts, 0, sizeof(uts));
  if (uname(&uts)) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Error, could not read kernel version: %s (%d)\n",
        strerror(errno), errno);
    return false;
  }

  first = strtol(uts.release, &next, 10);
  /* check for linux 3.x */
  if (first >= 3) {
    return true;
  }

  if (*next != '.') {
    goto kernel_parse_error;
  }

  second = strtol(next+1, &next, 10);
  if (*next != '.') {
    goto kernel_parse_error;
  }

  third = strtol(next+1, NULL, 10);

  /* better or equal than linux 2.6.31 ? */
  return first == 2 && second == 6 && third >= 31;

kernel_parse_error:
  OLSR_WARN(LOG_OS_SYSTEM,
      "Error, cannot parse kernel version: %s\n", uts.release);
  return false;
}

static void
_netlink_handler(int fd,
    void *data __attribute__((unused)),
    bool event_read,
    bool event_write __attribute__((unused))) {
  int len, plen;
  int ret;

  if (!event_read) {
    return;
  }

  if ((ret = recvmsg(fd, &_netlink_msg, MSG_DONTWAIT)) >= 0) {
    /*check message*/
    len = _netlink_header->nlmsg_len;
    plen = len - sizeof(_netlink_header);
    if (len > ret || plen < 0) {
      OLSR_WARN(LOG_OS_SYSTEM,
          "Malformed netlink message: len=%d left=%d plen=%d\n",
              len, ret, plen);
      return;
    }

    OLSR_DEBUG(LOG_OS_SYSTEM,
        "Netlink message received: type %d\n", _netlink_header->nlmsg_type);

    switch (_netlink_header->nlmsg_type) {
      case RTM_NEWLINK:
      case RTM_DELLINK:
        _handle_nl_link();
        break;

      case RTM_NEWADDR:
      case RTM_DELADDR:
        _handle_nl_addr();
        break;
      default:
        break;
    }
  }
  else if (errno != EAGAIN) {
    OLSR_WARN(LOG_OS_SYSTEM,"netlink recvmsg error: %s (%d)\n",
        strerror(errno), errno);
  }
}

static void
_handle_nl_link(void) {
  struct ifinfomsg *ifi;
  char if_name[IF_NAMESIZE];

  ifi = (struct ifinfomsg *) NLMSG_DATA(_netlink_header);

  if (if_indextoname(ifi->ifi_index, if_name) == NULL) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Failed to convert if-index to name: %d", ifi->ifi_index);
    return;
  }

  OLSR_DEBUG(LOG_OS_SYSTEM, "Linkstatus of interface '%s' changed", if_name);
  olsr_interface_trigger_change(if_name);
}

static void
_handle_nl_addr(void) {
  struct ifaddrmsg *ifa;

  char if_name[IF_NAMESIZE];

  ifa = (struct ifaddrmsg *) NLMSG_DATA(_netlink_header);

  if (if_indextoname(ifa->ifa_index, if_name) == NULL) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Failed to convert if-index to name: %d", ifa->ifa_index);
    return;
  }

  OLSR_DEBUG(LOG_OS_SYSTEM, "Address of interface '%s' changed", if_name);
  olsr_interface_trigger_change(if_name);
}
