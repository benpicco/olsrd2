
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
#include "common/list.h"
#include "common/autobuf.h"
#include "common/netaddr.h"
#include "olsr_logging.h"
#include "os_net.h"
#include "olsr_packet_socket.h"
#include "olsr.h"

static struct list_entity packet_sockets = { NULL, NULL };
static char input_buffer[65536];

/* refcounter */
OLSR_SUBSYSTEM_STATE(olsr_packet_refcount);

static void olsr_packet_event(int fd, void *data, enum olsr_sockethandler_flags flags);

void
olsr_packet_init(void) {
  if (olsr_subsystem_init(&olsr_packet_refcount))
    return;

  list_init_head(&packet_sockets);
}

void
olsr_packet_cleanup(void) {
  struct olsr_packet_socket *skt;

  if (olsr_subsystem_cleanup(&olsr_packet_refcount))
    return;

  while (!list_is_empty(&packet_sockets)) {
    skt = list_first_element(&packet_sockets, skt, node);

    olsr_packet_remove(skt);
  }
}

int
olsr_packet_add(struct olsr_packet_socket *pktsocket,
    union netaddr_socket *local) {
  int s = -1;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  memset(pktsocket, 0, sizeof(*pktsocket));

  /* Init socket */
  s = os_net_getsocket(local, OS_SOCKET_UDP, 0, LOG_SOCKET_STREAM);
  if (s < 0) {
    return -1;
  }

  if ((pktsocket->scheduler_entry = olsr_socket_add(
      s, olsr_packet_event, pktsocket, OLSR_SOCKET_READ)) == NULL) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Packet socket hookup to scheduler failed for %s\n",
        netaddr_socket_to_string(&buf, local));
    goto open_comport_error;
  }

  abuf_init(&pktsocket->out, 2048);
  list_add_tail(&packet_sockets, &pktsocket->node);
  memcpy(&pktsocket->local_socket, local, sizeof(*local));

  pktsocket->input_buffer = input_buffer;
  pktsocket->input_buffer_length = sizeof(input_buffer);
  return 0;

open_comport_error:
  if (s != -1) {
    os_close(s);
  }
  if (pktsocket->scheduler_entry) {
    olsr_socket_remove(pktsocket->scheduler_entry);
  }
  abuf_free(&pktsocket->out);
  return -1;
}

void
olsr_packet_remove(struct olsr_packet_socket *pktsocket) {
  if (list_node_added(&pktsocket->node)) {
    os_close(pktsocket->scheduler_entry->fd);
    olsr_socket_remove(pktsocket->scheduler_entry);
    list_remove(&pktsocket->node);
  }
}

int
olsr_packet_send(struct olsr_packet_socket *pktsocket, union netaddr_socket *remote,
    void *data, size_t length) {
  int result;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  if (pktsocket->out.len == 0) {
    /* no backlog of outgoing packets, try to send directly */
    result = os_sendto(pktsocket->scheduler_entry->fd, data, length, remote);
    if (result > 0) {
      /* successful */
      return 0;
    }

    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      OLSR_WARN(LOG_SOCKET_PACKET, "Cannot send UDP packet to %s: %s (%d)",
          netaddr_socket_to_string(&buf, remote), strerror(errno), errno);
      return -1;
    }
  }

  /* append destination */
  abuf_memcpy(&pktsocket->out, remote, sizeof(*remote));

  /* append data length */
  abuf_append_uint16(&pktsocket->out, length);

  /* append data */
  abuf_memcpy(&pktsocket->out, data, length);

  /* activate outgoing socket scheduler */
  olsr_socket_enable(pktsocket->scheduler_entry, OLSR_SOCKET_WRITE);
  return 0;
}

static void
olsr_packet_event(int fd, void *data, enum olsr_sockethandler_flags flags) {
  struct olsr_packet_socket *pktsocket = data;
  union netaddr_socket *skt, sock;
  uint16_t length;
  char *pkt;
  int result;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  if ((flags & OLSR_SOCKET_READ) != 0) {
    result = os_recvfrom(fd, pktsocket->input_buffer, pktsocket->input_buffer_length-1, &sock);
    if (result > 0 && pktsocket->parse_data != NULL) {
      /* null terminate it */
      pktsocket->input_buffer[pktsocket->input_buffer_length-1] = 0;

      /* received valid packet */
      pktsocket->parse_data(pktsocket, pktsocket->input_buffer, result);
    }
    else if (result < 0 && (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
      OLSR_WARN(LOG_SOCKET_PACKET, "Cannot read packet from socket %s: %s (%d)",
          netaddr_socket_to_string(&buf, &pktsocket->local_socket), strerror(errno), errno);
    }
  }

  if ((flags & OLSR_SOCKET_WRITE) != 0 && pktsocket->out.len == 0) {
    /* pointer to remote socket */
    skt = data;

    /* data area */
    pkt = data;
    pkt += sizeof(*skt);

    memcpy(&length, pkt, 2);
    pkt += 2;

    /* try to send packet */
    result = sendto(fd, data, length, 0, &skt->std, sizeof(*skt));
    if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
      /* try again later */
      return;
    }

    if (result < 0) {
      /* display error message */
      OLSR_WARN(LOG_SOCKET_PACKET, "Cannot send UDP packet to %s: %s (%d)",
          netaddr_socket_to_string(&buf, skt), strerror(errno), errno);
    }

    /* remove data from outgoing buffer (both for success and for final error */
    abuf_pull(&pktsocket->out, sizeof(*skt) + 2 + length);
  }

  if (pktsocket->out.len == 0) {
    olsr_socket_disable(pktsocket->scheduler_entry, OLSR_SOCKET_WRITE);
  }
}
