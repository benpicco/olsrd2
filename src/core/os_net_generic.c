
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

#include <errno.h>
#include <fcntl.h>

#include "common/common_types.h"
#include "common/netaddr.h"
#include "common/string.h"
#include "olsr_logging.h"
#include "os_net.h"

#if OS_NET_CONFIGSOCKET == OS_GENERIC
/**
 * Configure a network socket
 * @param sock filedescriptor
 * @param bindto ip/port to bind the socket to
 * @param flags type of socket (udp/tcp, blocking, multicast)
 * @param recvbuf size of input buffer for socket
 * @param log_src logging source for error messages
 * @return -1 if an error happened, 0 otherwise
 */
int
os_net_configsocket(int sock, union netaddr_socket *bindto,
    enum olsr_socket_opt flags, int recvbuf,
    enum log_source log_src __attribute__((unused))) {
  int yes;
  socklen_t addrlen;

#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  if ((flags & OS_SOCKET_BLOCKING) == 0) {
    if (os_net_set_nonblocking(sock)) {
      return 0;
    }
  }

#if defined(SO_REUSEADDR)
  /* allow to reuse address */
  yes = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof(yes)) < 0) {
    OLSR_WARN(log_src, "Cannot reuse address for %s: %s (%d)\n",
        netaddr_socket_to_string(&buf, bindto), strerror(errno), errno);
    return -1;
  }
#endif

#if defined(IP_RECVIF)
  if (setsockopt(sock, IPPROTO_IP, IP_RECVIF, (char *)&yes, sizeof(yes)) < 0) {
    OLSR_WARN(log_src, "Cannot apply IP_RECVIF for %s: %s (%d)\n",
        netaddr_socket_to_string(&buf, bindto), strerror(errno), errno);
    return -1;
  }
#endif

#if defined(SO_RCVBUF)
  if (recvbuf > 0) {
    while (recvbuf > 8192) {
      if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
          (char *)&recvbuf, sizeof(recvbuf)) == 0) {
        break;
      }

      recvbuf -= 1024;
    }

    if (recvbuf < 8192) {
      OLSR_WARN(log_src, "Cannot setup receive buffer size for %s: %s (%d)\n",
          netaddr_socket_to_string(&buf, bindto), strerror(errno), errno);
      return -1;
    }
  }
#endif

  if ((flags & OS_SOCKET_MULTICAST) != 0) {
#ifdef SO_BROADCAST
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0) {
      OLSR_WARN(log_src, "Cannot setup SO_BROADCAST for %s: %s (%d)\n",
          netaddr_socket_to_string(&buf, bindto), strerror(errno), errno);
      return -1;
    }
#endif
  }

  /* bind the socket to the port number */
  addrlen = sizeof(*bindto);
  if (bind(sock, &bindto->std, addrlen) < 0) {
    OLSR_WARN(log_src, "Cannot bind socket to %s: %s (%d)\n",
        netaddr_socket_to_string(&buf, bindto), strerror(errno), errno);
    return -1;
  }
  return 0;
}
#endif

#if OS_NET_GETSOCKET == OS_GENERIC
/**
 * Creates a new socket and configures it
 * @param bindto address to bind the socket to
 * @param flags type of socket (udp/tcp, blocking, multicast)
 * @param recvbuf size of input buffer for socket
 * @param log_src logging source for error messages
 * @return socket filedescriptor, -1 if an error happened
 */
int
os_net_getsocket(union netaddr_socket *bindto,
    enum olsr_socket_opt flags, int recvbuf,
    enum log_source log_src __attribute__((unused))) {

  int sock;

  sock = socket(bindto->std.sa_family,
      ((flags & OS_SOCKET_TCP) != 0)? SOCK_STREAM : SOCK_DGRAM, 0);
  if (sock < 0) {
    OLSR_WARN(log_src, "Cannot open socket: %s (%d)", strerror(errno), errno);
    return -1;
  }

  if (os_net_configsocket(sock, bindto, flags, recvbuf, log_src)) {
    close(sock);
    return -1;
  }
  return sock;
}
#endif

#if OS_NET_JOINMCAST == OS_GENERIC
/**
 * Join a socket into a multicast group
 * @param sock filedescriptor of socket
 * @param multicast multicast ip/port to join
 * @param oif pointer to outgoing interface for multicast
 * @param log_src logging source for error messages
 * @return -1 if an error happened, 0 otherwise
 */
int
net_os_join_mcast(int sock, union netaddr_socket *multicast,
    struct olsr_interface *oif,
    enum log_source log_src __attribute__((unused))) {
#if !defined (REMOVE_LOG_DEBUG)
  struct netaddr_str buf1, buf2;
#endif
  struct ip_mreq   v4_mreq;
  struct ipv6_mreq v6_mreq;
  char p;

  if (multicast->std.sa_family == AF_INET) {
    if (!IN_MULTICAST(ntohl(multicast->v4.sin_addr.s_addr))) {
      /* TODO: silent fail ? */
      return 0;
    }

    OLSR_DEBUG(log_src,
        "Socket on interface %s joining multicast %s (src %s)\n",
        oif->name,
        netaddr_socket_to_string(&buf2, multicast),
        netaddr_to_string(&buf1, &oif->local_v4));

    v4_mreq.imr_multiaddr = multicast->v4.sin_addr;
    netaddr_to_binary(&v4_mreq.imr_interface, &oif->local_v4, 4);

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
        &v4_mreq, sizeof(v4_mreq)) < 0) {
      OLSR_WARN(log_src, "Cannot join multicast group: %s (%d)\n", strerror(errno), errno);
      return -1;
    }

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, oif->local_v4.addr, 4) < 0) {
      OLSR_WARN(log_src, "Cannot set multicast interface: %s (%d)\n",
          strerror(errno), errno);
      return -1;
    }

    p = 0;
    if(setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&p, sizeof(p)) < 0) {
      OLSR_WARN(log_src, "Cannot deactivate local loop of multicast interface: %s (%d)\n",
          strerror(errno), errno);
      return -1;
    }
  }
  else {
    OLSR_DEBUG(log_src,
        "Socket on interface %s joining multicast %s (src %s)\n",
        oif->name,
        netaddr_socket_to_string(&buf2, multicast),
        netaddr_to_string(&buf1, &oif->local_v6));

    v6_mreq.ipv6mr_multiaddr = multicast->v6.sin6_addr;
    v6_mreq.ipv6mr_interface = oif->index;

    /* Send multicast */
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
        &v6_mreq, sizeof(v6_mreq)) < 0) {
      OLSR_WARN(log_src, "Cannot join multicast group: %s (%d)\n",
          strerror(errno), errno);
      return -1;
    }
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
        &oif->index, sizeof(oif->index)) < 0) {
      OLSR_WARN(log_src, "Cannot set multicast interface: %s (%d)\n",
          strerror(errno), errno);
      return -1;
    }
    p = 0;
    if(setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (char *)&p, sizeof(p)) < 0) {
      OLSR_WARN(log_src, "Cannot deactivate local loop of multicast interface: %s (%d)\n",
          strerror(errno), errno);
      return -1;
    }
  }
  return 0;
}
#endif

#if OS_NET_SETNONBLOCK == OS_GENERIC
/**
 * Set a socket to non-blocking mode
 * @param sock filedescriptor of socket
 * @return -1 if an error happened, 0 otherwise
 */
int
os_net_set_nonblocking(int sock) {
  int state;

  /* put socket into non-blocking mode */
  if ((state = fcntl(sock, F_GETFL)) == -1) {
    return -1;
  }

  if (fcntl(sock, F_SETFL, state | O_NONBLOCK) < 0) {
    return -1;
  }
  return 0;
}
#endif

#if OS_NET_BINDTOIF == OS_GENERIC
/**
 * Bind a socket to an interface. Filters out alias part of interface name.
 * @param sock filedescriptor of socket
 * @param if_name name of interface
 * @return -1 if an error happened, 0 otherwise
 */
int
os_net_bind_to_interface(int sock, const char *if_name) {
  char if_buf[IF_NAMESIZE];
  char *ptr;

  strscpy(if_buf, if_name, sizeof(if_buf));
  ptr = strchr(if_buf, ':');
  if (ptr) {
    *ptr = 0;
  }

  /* bind to device using the SO_BINDTODEVICE flag */
  return setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, if_buf, strlen(if_buf) + 1);
}
#endif
