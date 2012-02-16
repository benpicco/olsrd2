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
#include <errno.h>
#include <unistd.h>

#include "common/common_types.h"
#include "common/string.h"
#include "olsr_interface.h"
#include "olsr_socket.h"
#include "olsr.h"
#include "os_system.h"

static void _netlink_handler(int fd, void *data,
    bool event_read, bool event_write);
static void _handle_nl_link(struct nlmsghdr *);
static void _handle_nl_addr(struct nlmsghdr *);
static void _handle_nl_err(struct nlmsghdr *);

/* global procfile state before initialization */
static char _original_rp_filter;
static char _original_icmp_redirect;

/* rtnetlink sockets */
static int _rtnetlink_async_fd = -1;
static int _rtnetlink_sync_fd = -1;
static struct olsr_socket_entry _rtnetlink_socket = {
  .process = _netlink_handler,
  .event_read = true,
};

/* ioctl socket */
static int _ioctl_fd = -1;

/* static buffers for receiving/sending a netlink message */
static struct sockaddr_nl _netlink_nladdr = {
  .nl_family = AF_NETLINK
};

static struct iovec _netlink_rcv_iov;
static struct msghdr _netlink_rcv_msg = {
  &_netlink_nladdr,
  sizeof(_netlink_nladdr),
  &_netlink_rcv_iov,
  1,
  NULL,
  0,
  0
};

static struct iovec _netlink_send_iov;
static struct msghdr _netlink_send_msg = {
  &_netlink_nladdr,
  sizeof(_netlink_nladdr),
  &_netlink_send_iov,
  1,
  NULL,
  0,
  0
};

static struct nlmsghdr _netlink_hdr_done = {
  .nlmsg_len = sizeof(struct nlmsghdr),
  .nlmsg_type = NLMSG_DONE
};

/* buffer for incoming netlink messages */
struct nlmsghdr *_netlink_recv_buffer;

/* buffer for outgoing netlink messages */
struct autobuf _netlink_send_buffer;

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

  _rtnetlink_sync_fd = -1;
  _rtnetlink_async_fd = -1;
  _netlink_recv_buffer = NULL;

  _ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (_ioctl_fd == -1) {
    OLSR_WARN(LOG_OS_SYSTEM, "Cannot open ioctl socket: %s (%d)",
        strerror(errno), errno);
    return -1;
  }

  if (abuf_init(&_netlink_send_buffer, UIO_MAXIOV)) {
    OLSR_WARN_OOM(LOG_OS_SYSTEM);
    goto os_system_init_failed;
  }

  _netlink_recv_buffer = calloc(UIO_MAXIOV, 1);
  if (_netlink_recv_buffer == NULL) {
    OLSR_WARN_OOM(LOG_OS_SYSTEM);
    goto os_system_init_failed;
  }
  _netlink_rcv_iov.iov_base = _netlink_recv_buffer;
  _netlink_rcv_iov.iov_len = UIO_MAXIOV;

  _rtnetlink_async_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (_rtnetlink_async_fd < 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Cannot open async rtnetlink socket: %s (%d)",
        strerror(errno), errno);
    goto os_system_init_failed;
  }

  _rtnetlink_sync_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (_rtnetlink_sync_fd < 0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Cannot open sync rtnetlink socket: %s (%d)",
        strerror(errno), errno);
    goto os_system_init_failed;
  }

  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

  /* kernel will assign appropriate number instead of pid */
  /* addr.nl_pid = 0; */

  if (bind(_rtnetlink_async_fd, (struct sockaddr *)&addr, sizeof(addr))<0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not bind async rtnetlink socket: %s (%d)",
        strerror(errno), errno);
    goto os_system_init_failed;
  }

  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;

  /* kernel will assign appropriate number instead of pid */
  /* addr.nl_pid = 0; */

  if (bind(_rtnetlink_sync_fd, (struct sockaddr *)&addr, sizeof(addr))<0) {
    OLSR_WARN(LOG_OS_SYSTEM, "Could not bind sync rtnetlink socket: %s (%d)",
        strerror(errno), errno);
    goto os_system_init_failed;
  }

  /* add socket listener */
  _rtnetlink_socket.fd = _rtnetlink_async_fd;
  olsr_socket_add(&_rtnetlink_socket);

  /* mark both flags non-used */
  _original_icmp_redirect = 0;
  _original_rp_filter = 0;

  olsr_subsystem_init(&_os_system_state);
  return 0;
os_system_init_failed:
  if (_rtnetlink_sync_fd != -1)
    close (_rtnetlink_sync_fd);
  if (_rtnetlink_async_fd != -1)
    close (_rtnetlink_async_fd);
  free(_netlink_recv_buffer);
  abuf_free(&_netlink_send_buffer);
  close(_ioctl_fd);
  return -1;
}

/**
 * Cleanup os-specific subsystem
 */
void
os_system_cleanup(void) {
  if (olsr_subsystem_cleanup(&_os_system_state))
    return;

  close (_rtnetlink_sync_fd);
  close(_rtnetlink_async_fd);
  close(_ioctl_fd);

  abuf_free(&_netlink_send_buffer);
  free (_netlink_recv_buffer);
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
 * Add an attribute to a netlink message
 * @param n pointer to netlink header
 * @param type type of netlink attribute
 * @param data pointer to data of netlink attribute
 * @param len length of data of netlink attribute
 * @return -1 if netlink message got too large, 0 otherwise
 */
int
os_system_netlink_addreq(struct nlmsghdr *n,
    int type, const void *data, int len)
{
  struct rtattr *rta;
  size_t aligned_msg_len, aligned_attr_len;

  /* calculate aligned length of message and new attribute */
  aligned_msg_len = NLMSG_ALIGN(n->nlmsg_len);
  aligned_attr_len = RTA_LENGTH(len);

  if (aligned_msg_len + aligned_attr_len > UIO_MAXIOV) {
    OLSR_WARN(LOG_OS_SYSTEM, "Netlink message got too large!");
    return -1;
  }
  rta = (struct rtattr *)((void*)((char *)n + aligned_msg_len));
  rta->rta_type = type;
  rta->rta_len = aligned_attr_len;

  n->nlmsg_len = aligned_msg_len + aligned_attr_len;

  memcpy(RTA_DATA(rta), data, len);
  return 0;
}

/**
 * Sends a single netlink message to the kernel, blocks until it receives the
 * answer and checks the error code. The answer must only contain a single
 * NLMSG_ERROR message.
 * @param nl_hdr pointer to netlink header
 * @return negative if an error happened, 0 if everything was okay.
 */
int
os_system_netlink_sync_send(struct nlmsghdr *nl_hdr)
{
  struct nlmsgerr *l_err;
  int ret;

  _netlink_send_iov.iov_base = nl_hdr;
  _netlink_send_iov.iov_len = nl_hdr->nlmsg_len;

  if (sendmsg(_rtnetlink_sync_fd, &_netlink_send_msg, 0) <= 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Cannot send data to netlink socket (%d: %s)",
        errno, strerror(errno));
    return -1;
  }

  ret = recvmsg(_rtnetlink_sync_fd, &_netlink_rcv_msg, 0);
  if (ret <= 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Error while reading answer to netlink message (%d: %s)",
        errno, strerror(errno));
    return -1;
  }

  if (!NLMSG_OK(_netlink_recv_buffer, (unsigned int)ret)) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Received netlink message was malformed (ret=%d, %u)",
        ret, _netlink_recv_buffer->nlmsg_len);
    return -1;
  }

  if (_netlink_recv_buffer->nlmsg_type != NLMSG_ERROR) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Received unknown netlink response: %u bytes, type %u (not %u) with seqnr %u and flags %u from %u",
        _netlink_recv_buffer->nlmsg_len, _netlink_recv_buffer->nlmsg_type,
        NLMSG_ERROR, _netlink_recv_buffer->nlmsg_seq, _netlink_recv_buffer->nlmsg_flags, _netlink_recv_buffer->nlmsg_pid);
    return -1;
  }
  if (NLMSG_LENGTH(sizeof(struct nlmsgerr)) > _netlink_recv_buffer->nlmsg_len) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Received invalid netlink message size %zu != %u",
        sizeof(struct nlmsgerr), _netlink_recv_buffer->nlmsg_len);
    return -1;
  }

  l_err = (struct nlmsgerr *)NLMSG_DATA(_netlink_recv_buffer);

  if (l_err->error) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Received netlink error code %s (%d)",
        strerror(-l_err->error), l_err->error);
  }
  return -l_err->error;
}

/**
 * Sends a single netlink message to the kernel. The answer will be delivered
 * to the netlink listener.
 * @param nl_hdr pointer to netlink header
 * @return sequence number of buffered netlink message
 */
int
os_system_netlink_async_send(struct nlmsghdr *nl_hdr) {
  static int nl_seq = 0;

  if (nl_seq < 0) {
    nl_seq = 0;
  }
  nl_seq++;

  nl_hdr->nlmsg_seq = nl_seq;
  nl_hdr->nlmsg_flags |= NLM_F_MULTI;

  abuf_memcpy(&_netlink_send_buffer, nl_hdr, nl_hdr->nlmsg_len);

  /* trigger write */
  olsr_socket_set_write(&_rtnetlink_socket, true);
  return nl_seq;
}

static void
_flush_netlink_buffer(void) {
  ssize_t ret;

  /* add DONE message */
  abuf_memcpy(&_netlink_send_buffer, &_netlink_hdr_done, sizeof(_netlink_hdr_done));

  /* send outgoing message */
  _netlink_send_iov.iov_base = _netlink_send_buffer.buf;
  _netlink_send_iov.iov_len = _netlink_send_buffer.len;

  if ((ret = sendmsg(_rtnetlink_async_fd, &_netlink_send_msg, 0)) <= 0) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Cannot send data to netlink socket (%d: %s)",
        errno, strerror(errno));
  }
  else {
    abuf_clear(&_netlink_send_buffer);
  }

  if (_netlink_send_buffer.len == 0) {
    olsr_socket_set_write(&_rtnetlink_socket, false);
  }
}

/**
 * Handler for incoming async netlink messages
 * @param fd
 * @param data
 * @param event_read
 * @param event_write
 */
static void
_netlink_handler(int fd,
    void *data __attribute__((unused)),
    bool event_read,
    bool event_write) {
  ssize_t ret;
  size_t len;
  struct nlmsghdr *nh;

  if (event_write) {
    _flush_netlink_buffer();
  }

  if (!event_read) {
    return;
  }

  /* handle incoming messages */
  if ((ret = recvmsg(fd, &_netlink_rcv_msg, MSG_DONTWAIT)) < 0) {
    if (errno != EAGAIN) {
      OLSR_WARN(LOG_OS_SYSTEM,"netlink recvmsg error: %s (%d)\n",
          strerror(errno), errno);
    }
    return;
  }

  /* loop through netlink headers */
  len = (size_t) ret;
  for (nh = _netlink_recv_buffer; NLMSG_OK (nh, len);
       nh = NLMSG_NEXT (nh, len)) {

    OLSR_DEBUG(LOG_OS_SYSTEM,
        "Netlink message received: type %d\n", _netlink_recv_buffer->nlmsg_type);

    switch (nh->nlmsg_type) {
      case NLMSG_NOOP:
        break;

      case NLMSG_DONE:
        /* End of a multipart netlink message reached */
        return;

      case NLMSG_ERROR:
        /* Feedback for async netlink message */
        _handle_nl_err(nh);
        break;

      case RTM_NEWLINK:
      case RTM_DELLINK:
        /* link up/down */
        _handle_nl_link(nh);
        break;

      case RTM_NEWADDR:
      case RTM_DELADDR:
        /* address added/removed */
        _handle_nl_addr(nh);
        break;

      default:
        break;
    }
  }
}

/**
 * Handle incoming RTM_NEWLINK/RTM_DELLINK netlink messages
 * @param nl pointer to netlink message header
 */
static void
_handle_nl_link(struct nlmsghdr *nl) {
  struct ifinfomsg *ifi;
  char if_name[IF_NAMESIZE];

  ifi = (struct ifinfomsg *) NLMSG_DATA(nl);

  if (if_indextoname(ifi->ifi_index, if_name) == NULL) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Failed to convert if-index to name: %d", ifi->ifi_index);
    return;
  }

  OLSR_DEBUG(LOG_OS_SYSTEM, "Linkstatus of interface '%s' changed", if_name);
  olsr_interface_trigger_change(if_name);
}

/**
 * Handle incoming RTM_NEWADDR/RTM_DELADDR netlink messages
 * @param nl pointer to netlink message header
 */
static void
_handle_nl_addr(struct nlmsghdr *nl) {
  struct ifaddrmsg *ifa;

  char if_name[IF_NAMESIZE];

  ifa = (struct ifaddrmsg *) NLMSG_DATA(nl);

  if (if_indextoname(ifa->ifa_index, if_name) == NULL) {
    OLSR_WARN(LOG_OS_SYSTEM,
        "Failed to convert if-index to name: %d", ifa->ifa_index);
    return;
  }

  OLSR_DEBUG(LOG_OS_SYSTEM, "Address of interface '%s' changed", if_name);
  olsr_interface_trigger_change(if_name);
}

static void
_handle_nl_err(struct nlmsghdr *nl) {
  struct nlmsgerr *err;

  err = (struct nlmsgerr *) NLMSG_DATA(nl);

  if (err->msg.nlmsg_type == RTM_NEWROUTE
      || err->msg.nlmsg_type == RTM_DELROUTE) {
    OLSR_DEBUG(LOG_OS_SYSTEM, "Got feedback for routing seq %d: %d", nl->nlmsg_seq, err->error);
  }
}
