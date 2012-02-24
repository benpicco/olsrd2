
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
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

static void _cb_rtnetlink_feedback(uint32_t seq, int error);
static void _cb_rtnetlink_timeout(void);

/* global procfile state before initialization */
static char _original_rp_filter;
static char _original_icmp_redirect;

/* netlink socket for route set/get commands */
struct os_system_netlink _rtnetlink_socket;

OLSR_SUBSYSTEM_STATE(_os_routing_state);

/**
 * Initialize routing subsystem
 * @return -1 if an error happened, 0 otherwise
 */
int
os_routing_init(void) {
  if (olsr_subsystem_is_initialized(&_os_routing_state))
    return 0;

  if (os_system_netlink_add(&_rtnetlink_socket, NETLINK_ROUTE, 0)) {
    return -1;
  }

  _rtnetlink_socket.cb_feedback = _cb_rtnetlink_feedback;
  _rtnetlink_socket.cb_timeout = _cb_rtnetlink_timeout;

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

/**
 * Cleanup all resources allocated by the routing subsystem
 */
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

  os_system_netlink_remove(&_rtnetlink_socket);
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

static int
_routing_set(struct nlmsghdr *msg,
    const struct netaddr *src, const struct netaddr *gw, const struct netaddr *dst,
    int rttable, int if_index, int metric, int protocol, unsigned char scope);

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
os_routing_set(const struct netaddr *src, const struct netaddr *gw, const struct netaddr *dst,
    int rttable, int if_index, int metric, int protocol, bool set, bool del_similar) {
  uint8_t buffer[UIO_MAXIOV];
  struct nlmsghdr *msg;
  unsigned char scope;

  memset(buffer, 0, sizeof(buffer));

  /* get pointers for netlink message */
  msg = (void *)&buffer[0];

  msg->nlmsg_flags = NLM_F_REQUEST;

  /* set length of netlink message with rtmsg payload */
  msg->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));

  /* normally all routing operations are UNIVERSE scope */
  scope = RT_SCOPE_UNIVERSE;

  if (set) {
    msg->nlmsg_flags |= NLM_F_CREATE | NLM_F_REPLACE;
    msg->nlmsg_type = RTM_NEWROUTE;
  } else {
    msg->nlmsg_type = RTM_DELROUTE;

    protocol = -1;
    src = NULL;

    if (del_similar) {
      /* no interface necessary */
      if_index = -1;

      /* as wildcard for fuzzy deletion */
      scope = RT_SCOPE_NOWHERE;
    }
  }

  if (gw == NULL && dst->prefix_len == netaddr_get_maxprefix(dst)) {
    /* use destination as gateway, to 'force' linux kernel to do proper source address selection */
    gw = dst;
  }

  if (_routing_set(msg, src, gw, dst, rttable, if_index, metric, protocol, scope)) {
    return -1;
  }

  return os_system_netlink_send(&_rtnetlink_socket, msg);
}

/**
 * Initiatize the an netlink routing message
 * @param msg pointer to netlink message header
 * @param src source address of route (NULL if not set)
 * @param gw gateway of route (NULL if not set)
 * @param dst destination prefix of route (NULL if not set)
 * @param rttable routing table (mandatory)
 * @param if_index interface index, -1 if not set
 * @param metric routing metric, -1 if not set
 * @param protocol routing protocol, -1 if not set
 * @param scope scope of route to be set/removed
 * @return -1 if an error happened, 0 otherwise
 */
static int
_routing_set(struct nlmsghdr *msg,
    const struct netaddr *src, const struct netaddr *gw, const struct netaddr *dst,
    int rttable, int if_index, int metric, int protocol, unsigned char scope) {
  struct rtmsg *rt_msg;
  int type = -1;

  /* calculate address type */
  if (dst) {
    type = dst->type;
  }
  if (gw) {
    if (type != -1 && type != gw->type) {
      return -1;
    }
    type = gw->type;
  }
  if (src) {
    if (type != -1 && type != src->type) {
      return -1;
    }
    type = gw->type;
  }

  /* and do a consistency check */
  assert (type == AF_INET || type == AF_INET6);

  /* initialize rtmsg payload */
  rt_msg = NLMSG_DATA(msg);

  rt_msg->rtm_family = type;
  rt_msg->rtm_scope = scope;

  /*
   * RTN_UNSPEC would be the wildcard,
   * but blackhole, broadcast or NAT rules should usually not conflict
   */
  /* -> olsr only adds deletes unicast routes at the moment */
  rt_msg->rtm_type = RTN_UNICAST;

  if (protocol != -1) {
    /* set protocol */
    rt_msg->rtm_protocol = protocol;
  }

  if (rttable != -1) {
    /* set routing table */
    rt_msg->rtm_table = rttable;
  }

  /* add attributes */
  if (src != NULL) {
    rt_msg->rtm_src_len = src->prefix_len;

    /* add src-ip */
    if (os_system_netlink_addnetaddr(msg, RTA_PREFSRC, src)) {
      return -1;
    }
  }

  if (gw != NULL) {
    rt_msg->rtm_flags = RTNH_F_ONLINK;

    /* add gateway */
    if (os_system_netlink_addnetaddr(msg, RTA_GATEWAY, gw)) {
      return -1;
    }
  }

  if (dst != 0) {
    rt_msg->rtm_dst_len = dst->prefix_len;

    /* add destination */
    if (os_system_netlink_addnetaddr(msg, RTA_DST, dst)) {
      return -1;
    }
  }

  if (metric != -1) {
    /* add metric */
    if (os_system_netlink_addreq(msg, RTA_PRIORITY, &metric, sizeof(metric))) {
      return -1;
    }
  }

  if (if_index != -1) {
    /* add interface*/
    if (os_system_netlink_addreq(msg, RTA_OIF, &if_index, sizeof(if_index))) {
      return -1;
    }
  }
  return 0;
}

/**
 * TODO: Handle feedback from netlink socket
 * @param seq
 * @param error
 */
static void
_cb_rtnetlink_feedback(uint32_t seq, int error) {
  OLSR_INFO(LOG_OS_ROUTING, "Got feedback: %d %d", seq, error);
}

/**
 * TODO: Handle ack timeout from netlink socket
 */
static void
_cb_rtnetlink_timeout(void) {
  OLSR_INFO(LOG_OS_ROUTING, "Got timeout");
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

/**
 * @return true if linux kernel is at least 2.6.31
 */
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
