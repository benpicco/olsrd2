
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2012, the olsr.org team - see HISTORY file
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
#include "olsr_interface.h"
#include "os_net.h"

/**
 * Join a socket into a multicast group
 * @param sock filedescriptor of socket
 * @param multicast multicast ip/port to join
 * @param oif pointer to outgoing interface data for multicast
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
    OLSR_DEBUG(log_src,
        "Socket on interface %s joining multicast %s (src %s)\n",
        oif->data.name,
        netaddr_socket_to_string(&buf2, multicast),
        netaddr_to_string(&buf1, &oif->data.if_v4));

    v4_mreq.imr_multiaddr = multicast->v4.sin_addr;
    netaddr_to_binary(&v4_mreq.imr_interface, &oif->data.if_v4, 4);

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
        &v4_mreq, sizeof(v4_mreq)) < 0) {
      OLSR_WARN(log_src, "Cannot join multicast group: %s (%d)\n", strerror(errno), errno);
      return -1;
    }

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, oif->data.if_v4.addr, 4) < 0) {
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
        oif->data.name,
        netaddr_socket_to_string(&buf2, multicast),
        netaddr_to_string(&buf1, &oif->data.linklocal_v6));

    v6_mreq.ipv6mr_multiaddr = multicast->v6.sin6_addr;
    v6_mreq.ipv6mr_interface = oif->data.index;

    /* Send multicast */
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
        &v6_mreq, sizeof(v6_mreq)) < 0) {
      OLSR_WARN(log_src, "Cannot join multicast group: %s (%d)\n",
          strerror(errno), errno);
      return -1;
    }
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
        &oif->data.index, sizeof(oif->data.index)) < 0) {
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
