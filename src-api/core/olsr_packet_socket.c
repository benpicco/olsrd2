
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
#include "common/list.h"
#include "common/autobuf.h"
#include "common/netaddr.h"
#include "os_net.h"
#include "olsr_cfg.h"
#include "olsr_logging.h"
#include "olsr_packet_socket.h"
#include "olsr.h"

static struct list_entity packet_sockets = { NULL, NULL };
static char input_buffer[65536];

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(_packet_state);

static int _apply_managed_socket(struct olsr_packet_managed *managed,
    struct olsr_packet_socket *stream,
    struct netaddr *bindto, uint16_t port);
static void _cb_packet_event(int fd, void *data, bool r, bool w);

/**
 * Initialize packet socket handler
 */
void
olsr_packet_init(void) {
  if (olsr_subsystem_init(&_packet_state))
    return;

  list_init_head(&packet_sockets);
}

/**
 * Cleanup all resources allocated by packet socket handler
 */
void
olsr_packet_cleanup(void) {
  struct olsr_packet_socket *skt;

  if (olsr_subsystem_cleanup(&_packet_state))
    return;

  while (!list_is_empty(&packet_sockets)) {
    skt = list_first_element(&packet_sockets, skt, node);

    olsr_packet_remove(skt, true);
  }
}

/**
 * Add a new packet socket handler
 * @param pktsocket pointer to an initialized packet socket struct
 * @param local pointer local IP address of packet socket
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_packet_add(struct olsr_packet_socket *pktsocket,
    union netaddr_socket *local) {
  int s = -1;

  /* Init socket */
  s = os_net_getsocket(local, OS_SOCKET_UDP | OS_SOCKET_MULTICAST, 0, LOG_SOCKET_PACKET);
  if (s < 0) {
    return -1;
  }

  pktsocket->scheduler_entry.fd = s;
  pktsocket->scheduler_entry.process = _cb_packet_event;
  pktsocket->scheduler_entry.event_read = true;
  pktsocket->scheduler_entry.event_write = false;
  pktsocket->scheduler_entry.data = pktsocket;

  olsr_socket_add(&pktsocket->scheduler_entry);

  abuf_init(&pktsocket->out);
  list_add_tail(&packet_sockets, &pktsocket->node);
  memcpy(&pktsocket->local_socket, local, sizeof(pktsocket->local_socket));

  if (pktsocket->config.input_buffer_length == 0) {
    pktsocket->config.input_buffer = input_buffer;
    pktsocket->config.input_buffer_length = sizeof(input_buffer);
  }
  return 0;
}

/**
 * Remove a packet socket from the global scheduler
 * @param pktsocket pointer to packet socket
 */
void
olsr_packet_remove(struct olsr_packet_socket *pktsocket,
    bool force __attribute__((unused))) {
  if (list_is_node_added(&pktsocket->node)) {
    olsr_socket_remove(&pktsocket->scheduler_entry);
    os_close(pktsocket->scheduler_entry.fd);
    abuf_free(&pktsocket->out);

    list_remove(&pktsocket->node);
  }
}

/**
 * Send a data packet through a packet socket. The transmission might not
 * be happen synchronously if the socket would block.
 * @param pktsocket pointer to packet socket
 * @param remote ip/address to send packet to
 * @param data pointer to data to be sent
 * @param length length of data
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_packet_send(struct olsr_packet_socket *pktsocket, union netaddr_socket *remote,
    const void *data, size_t length) {
  int result;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  if (abuf_getlen(&pktsocket->out) == 0) {
    /* no backlog of outgoing packets, try to send directly */
    result = os_sendto(pktsocket->scheduler_entry.fd, data, length, remote);
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
  olsr_socket_set_write(&pktsocket->scheduler_entry, true);
  return 0;
}

void
olsr_packet_add_managed(struct olsr_packet_managed *managed) {
  if (managed->config.input_buffer_length == 0) {
    managed->config.input_buffer = input_buffer;
    managed->config.input_buffer_length = sizeof(input_buffer);
  }
}

void
olsr_packet_remove_managed(struct olsr_packet_managed *managed, bool forced) {
  olsr_packet_remove(&managed->socket_v4, forced);
  olsr_packet_remove(&managed->socket_v6, forced);

  olsr_acl_remove(&managed->acl);
}

int
olsr_packet_apply_managed(struct olsr_packet_managed *managed,
    struct olsr_packet_managed_config *config) {
  olsr_acl_copy(&managed->acl, &config->acl);

  if (config_global.ipv4) {
    if (_apply_managed_socket(managed,
        &managed->socket_v4, &config->bindto_v4, config->port)) {
      return -1;
    }
  }
  else {
    olsr_packet_remove(&managed->socket_v4, true);
  }

  if (config_global.ipv6) {
    if (_apply_managed_socket(managed,
        &managed->socket_v6, &config->bindto_v6, config->port)) {
      return -1;
    }
  }
  else {
    olsr_packet_remove(&managed->socket_v6, true);
  }
  return 0;
}

/**
 * Apply new configuration to a managed stream socket
 * @param managed pointer to managed stream
 * @param stream pointer to TCP stream to configure
 * @param bindto local address to bind socket to
 * @param port local port number
 * @return -1 if an error happened, 0 otherwise.
 */
static int
_apply_managed_socket(struct olsr_packet_managed *managed,
    struct olsr_packet_socket *packet,
    struct netaddr *bindto, uint16_t port) {
  union netaddr_socket sock;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  if (netaddr_socket_init(&sock, bindto, port)) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Cannot create managed socket address: %s/%u",
        netaddr_to_string(&buf, bindto), port);
    return -1;
  }

  if (memcmp(&sock, &packet->local_socket, sizeof(sock)) == 0) {
    /* nothing changed */
    return 0;
  }

  olsr_packet_remove(packet, true);
  if (olsr_packet_add(packet, &sock)) {
    return -1;
  }

  /* copy configuration */
  memcpy(&packet->config, &managed->config, sizeof(packet->config));
  return 0;
}

/**
 * Callback to handle data from the olsr socket scheduler
 * @param fd filedescriptor to read data from
 * @param data custom data pointer
 * @param event_read true if read-event is incoming
 * @param event_write true if write-event is incoming
 */
static void
_cb_packet_event(int fd, void *data, bool event_read, bool event_write) {
  struct olsr_packet_socket *pktsocket = data;
  union netaddr_socket *skt, sock;
  uint16_t length;
  char *pkt;
  int result;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str netbuf;
#endif

  OLSR_DEBUG(LOG_SOCKET_PACKET, "UDP event.");

  if (event_read) {
    uint8_t *buf;

    /* handle incoming data */
    buf = pktsocket->config.input_buffer;

    result = os_recvfrom(fd, buf, pktsocket->config.input_buffer_length-1, &sock);
    if (result > 0 && pktsocket->config.receive_data != NULL) {
      /* null terminate it */
      buf[result] = 0;

      /* received valid packet */
      pktsocket->config.receive_data(pktsocket, &sock, result);
    }
    else if (result < 0 && (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
      OLSR_WARN(LOG_SOCKET_PACKET, "Cannot read packet from socket %s: %s (%d)",
          netaddr_socket_to_string(&netbuf, &pktsocket->local_socket), strerror(errno), errno);
    }
  }

  if (event_write && abuf_getlen(&pktsocket->out) > 0) {
    /* handle outgoing data */

    /* pointer to remote socket */
    skt = data;

    /* data area */
    pkt = data;
    pkt += sizeof(*skt);

    memcpy(&length, pkt, 2);
    pkt += 2;

    /* try to send packet */
    result = os_sendto(fd, data, length, skt);
    if (result < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
      /* try again later */
      return;
    }

    if (result < 0) {
      /* display error message */
      OLSR_WARN(LOG_SOCKET_PACKET, "Cannot send UDP packet to %s: %s (%d)",
          netaddr_socket_to_string(&netbuf, skt), strerror(errno), errno);
    }

    /* remove data from outgoing buffer (both for success and for final error */
    abuf_pull(&pktsocket->out, sizeof(*skt) + 2 + length);
  }

  if (abuf_getlen(&pktsocket->out) == 0) {
    /* nothing left to send, disable outgoing events */
    olsr_socket_set_write(&pktsocket->scheduler_entry, false);
  }
}
