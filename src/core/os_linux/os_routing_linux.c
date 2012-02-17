/*
 * os_routing_linux.c
 *
 *  Created on: Feb 13, 2012
 *      Author: rogge
 */

/* must be first because of a problem with linux/rtnetlink.h */
#include <sys/socket.h>

/* and now the rest of the includes */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/utsname.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "common/common_types.h"
#include "olsr_timer.h"
#include "olsr.h"
#include "os_routing.h"
#include "os_system.h"

/* ip forwarding */
#define PROC_IPFORWARD_V4 "/proc/sys/net/ipv4/ip_forward"
#define PROC_IPFORWARD_V6 "/proc/sys/net/ipv6/conf/all/forwarding"

/* Redirect proc entry */
#define PROC_IF_REDIRECT "/proc/sys/net/ipv4/conf/%s/send_redirects"
#define PROC_ALL_REDIRECT "/proc/sys/net/ipv4/conf/all/send_redirects"

/* IP spoof proc entry */
#define PROC_IF_SPOOF "/proc/sys/net/ipv4/conf/%s/rp_filter"
#define PROC_ALL_SPOOF "/proc/sys/net/ipv4/conf/all/rp_filter"

static bool _is_at_least_linuxkernel_2_6_31(void);
static int _os_linux_writeToProc(const char *file, char *old, char value);

/* global procfile state before initialization */
static char _original_rp_filter;
static char _original_icmp_redirect;

/* buffer for outgoing rtnetlink messages */
static struct nlmsghdr *_rtnetlink_buffer;

OLSR_SUBSYSTEM_STATE(_os_routing_state);

int
os_routing_init(void) {
  if (olsr_subsystem_is_initialized(&_os_routing_state))
    return 0;

  _rtnetlink_buffer = calloc(UIO_MAXIOV, 1);
  if (_rtnetlink_buffer == NULL) {
    OLSR_WARN_OOM(LOG_OS_SYSTEM);
    return -1;
  }

  if (_os_linux_writeToProc(PROC_ALL_REDIRECT, &_original_icmp_redirect, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable ICMP redirects! "
        "You should manually ensure that ICMP redirects are disabled!");
  }

  /* check kernel version and disable global rp_filter */
  if (_is_at_least_linuxkernel_2_6_31()) {
    if (_os_linux_writeToProc(PROC_ALL_SPOOF, &_original_rp_filter, '0')) {
      OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable global rp_filter "
          "(necessary for kernel 2.6.31 and newer)! You should manually "
          "ensure that rp_filter is disabled!");
    }
  }

  olsr_subsystem_init(&_os_routing_state);
  return 0;
}

void
os_routing_cleanup(void) {
  if (olsr_subsystem_cleanup(&_os_routing_state))
    return;

  if (_original_icmp_redirect != 0
      && _os_linux_writeToProc(PROC_ALL_REDIRECT, &_original_icmp_redirect, '0') != 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "WARNING! Could not restore ICMP redirect flag %s to %c!",
        PROC_ALL_REDIRECT, _original_icmp_redirect);
  }

  /* check kernel version and disable global rp_filter */
  if (_os_linux_writeToProc(PROC_ALL_SPOOF, &_original_rp_filter, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "WARNING! Could not restore global rp_filter flag %s to %c!",
        PROC_ALL_SPOOF, _original_rp_filter);
  }

  free (_rtnetlink_buffer);
}

/**
 * Initialize interface for mesh usage
 * @param interf pointer to interface object
 * @return -1 if an error happened, 0 otherwise
 */
int
os_routing_init_mesh_if(struct olsr_interface *interf) {
  char procfile[FILENAME_MAX];
  char old_redirect = 0, old_spoof = 0;

  if (!olsr_subsystem_is_initialized(&_os_routing_state)) {
    /* make interface listener work without routing core */
    return 0;
  }

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_REDIRECT, interf->name);

  if (_os_linux_writeToProc(procfile, &old_redirect, '0')) {
    OLSR_WARN(LOG_OS_SYSTEM, "WARNING! Could not disable ICMP redirects! "
        "You should manually ensure that ICMP redirects are disabled!");
  }

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_SPOOF, interf->name);

  if (_os_linux_writeToProc(procfile, &old_spoof, '0')) {
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
os_routing_cleanup_mesh_if(struct olsr_interface *interf) {
  char restore_redirect, restore_spoof;
  char procfile[FILENAME_MAX];

  if (!olsr_subsystem_is_initialized(&_os_routing_state)) {
    /* make interface listener work without routing core */
    return;
  }

  restore_redirect = (interf->_original_state >> 8) & 255;
  restore_spoof = (interf->_original_state & 255);

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_REDIRECT, interf->name);

  if (restore_redirect != 0
      && _os_linux_writeToProc(procfile, NULL, restore_redirect) != 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not restore ICMP redirect flag %s to %c",
        procfile, restore_redirect);
  }

  /* Generate the procfile name */
  snprintf(procfile, sizeof(procfile), PROC_IF_SPOOF, interf->name);

  if (restore_spoof != 0
      && _os_linux_writeToProc(procfile, NULL, restore_spoof) != 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not restore IP spoof flag %s to %c",
        procfile, restore_spoof);
  }

  interf->_original_state = 0;
  return;
}

/**
 * Update an entry of the kernel routing table. This call will only trigger
 * the change, the real change will be done as soon as the netlink socket is
 * writable.
 * @param src source address of route (NULL if source ip not set)
 * @param gw gateway of route (NULL if direct connection)
 * @param dst destination prefix of route
 * @param rttable routing table
 * @param if_index interface index
 * @param metric routing metric
 * @param protocol routing protocol
 * @param set true if route should be set, false if it should be removed
 * @param del_similar true if similar routes that block this one should be
 *   removed.
 * @return -1 if an error happened, rtnetlink sequence number otherwise
 */
int
os_routing_set(struct olsr_system_feedback *feedback,
    const struct netaddr *src, const struct netaddr *gw, const struct netaddr *dst,
    int rttable, int if_index, int metric, int protocol, bool set, bool del_similar) {
  struct rtmsg *rt_msg;

  assert(olsr_subsystem_is_initialized(&_os_routing_state));

  /* consistency check */
  if (dst == NULL || (dst->type != AF_INET && dst->type != AF_INET6)) {
    return -1;
  }
  if ((src != NULL && src->type != dst->type)
      || (gw != NULL && gw->type != dst->type)) {
    return -1;
  }

  memset(_rtnetlink_buffer, 0, sizeof(*_rtnetlink_buffer));

  rt_msg = NLMSG_DATA(_rtnetlink_buffer);
  memset(rt_msg, 0, sizeof(*rt_msg));

  rt_msg->rtm_flags = RTNH_F_ONLINK;
  rt_msg->rtm_family = dst->type;
  rt_msg->rtm_table = rttable;

  _rtnetlink_buffer->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  _rtnetlink_buffer->nlmsg_flags = NLM_F_REQUEST;

  if (set) {
    _rtnetlink_buffer->nlmsg_flags |= NLM_F_CREATE | NLM_F_REPLACE;
    _rtnetlink_buffer->nlmsg_type = RTM_NEWROUTE;
  } else {
    _rtnetlink_buffer->nlmsg_type = RTM_DELROUTE;
  }

  /* RTN_UNSPEC would be the wildcard, but blackhole broadcast or nat roules should usually not conflict */
  /* -> olsr only adds deletes unicast routes */
  rt_msg->rtm_type = RTN_UNICAST;

  rt_msg->rtm_dst_len = dst->prefix_len;

  if (set) {
    /* add protocol for setting a route */
    rt_msg->rtm_protocol = protocol;
  }

  /* calculate scope of operation */
  if (!set && del_similar) {
    /* as wildcard for fuzzy deletion */
    rt_msg->rtm_scope = RT_SCOPE_NOWHERE;
  }
  else {
    /* for all our routes */
    rt_msg->rtm_scope = RT_SCOPE_UNIVERSE;
  }

  if (set || !del_similar) {
    /* add interface*/
    os_system_netlink_addreq(_rtnetlink_buffer, RTA_OIF, &if_index, sizeof(if_index));
  }

  if (set && src != NULL) {
    /* add src-ip */
    os_system_netlink_addnetaddr(_rtnetlink_buffer, RTA_PREFSRC, src);
  }

  if (metric != -1) {
    /* add metric */
    os_system_netlink_addreq(_rtnetlink_buffer, RTA_PRIORITY, &metric, sizeof(metric));
  }

  if (gw) {
    /* add gateway */
    os_system_netlink_addnetaddr(_rtnetlink_buffer, RTA_GATEWAY, gw);
  }
  else if ( dst->prefix_len == 32 ) {
    /* use destination as gateway, to 'force' linux kernel to do proper source address selection */
    os_system_netlink_addnetaddr(_rtnetlink_buffer, RTA_GATEWAY, dst);
  }
  else {
    /*
     * do not use onlink on such routes(no gateway, but no hostroute aswell)
     * e.g. smartgateway default route over an ptp tunnel interface
     */
    rt_msg->rtm_flags &= (~RTNH_F_ONLINK);
  }

   /* add destination */
  os_system_netlink_addnetaddr(_rtnetlink_buffer, RTA_DST, dst);

  return os_system_netlink_async_send(feedback, _rtnetlink_buffer);
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
_os_linux_writeToProc(const char *file, char *old, char value) {
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
