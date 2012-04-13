
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
 * Configure a network socket
 * @param sock filedescriptor
 * @param bindto ip/port to bind the socket to
 * @param flags type of socket (udp/tcp, blocking, multicast)
 * @param recvbuf size of input buffer for socket
 * @param log_src logging source for error messages
 * @return -1 if an error happened, 0 otherwise
 */
int
os_net_configsocket(int sock, union netaddr_socket *_bindto, int recvbuf,
    struct olsr_interface_data *data __attribute__((unused)),
    enum log_source log_src __attribute__((unused))) {
  int yes;
  socklen_t addrlen;
  union netaddr_socket bindto;

#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  /* temporary copy bindto address */
  memcpy(&bindto, _bindto, sizeof(bindto));

  if (os_net_set_nonblocking(sock)) {
    return 0;
  }

#if defined(SO_BINDTODEVICE)
  if (data != NULL && setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
      data->name, strlen(data->name) + 1) < 0) {
    OLSR_WARN(log_src, "Cannot bind socket to interface %s: %s (%d)\n",
        data->name, strerror(errno), errno);
    return -1;
  }
#endif

#if defined(SO_REUSEADDR)
  /* allow to reuse address */
  yes = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    OLSR_WARN(log_src, "Cannot reuse address for %s: %s (%d)\n",
        netaddr_socket_to_string(&buf, &bindto), strerror(errno), errno);
    return -1;
  }
#endif

#if defined(IP_RECVIF)
  if (data != NULL
      && setsockopt(sock, IPPROTO_IP, IP_RECVIF, &yes, sizeof(yes)) < 0) {
    OLSR_WARN(log_src, "Cannot apply IP_RECVIF for %s: %s (%d)\n",
        netaddr_socket_to_string(&buf, &bindto), strerror(errno), errno);
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
          netaddr_socket_to_string(&buf, &bindto), strerror(errno), errno);
      return -1;
    }
  }
#endif

  /* add ipv6 interface scope if necessary */
  if (data != NULL && netaddr_socket_get_addressfamily(&bindto) == AF_INET6) {
    bindto.v6.sin6_scope_id = data->index;
  }

  /* bind the socket to the port number */
  addrlen = sizeof(bindto);
  if (bind(sock, &bindto.std, addrlen) < 0) {
    OLSR_WARN(log_src, "Cannot bind socket to address %s: %s (%d)\n",
        netaddr_socket_to_string(&buf, &bindto), strerror(errno), errno);
    return -1;
  }
  return 0;
}
