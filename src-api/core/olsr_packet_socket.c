
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

static int _apply_managed(struct olsr_packet_managed *managed,
    struct olsr_packet_managed_config *config);
static int _apply_managed_socketpair(struct olsr_packet_managed *managed,
    struct olsr_interface_data *data,
    struct olsr_packet_socket *sock, struct netaddr *bind_ip, uint16_t port,
    struct olsr_packet_socket *mc_sock, struct netaddr *mc_ip, uint16_t mc_port,
    bool mc_loopback);
static int _apply_managed_socket(struct olsr_packet_socket *stream,
    struct netaddr *bindto, uint16_t port, struct olsr_interface_data *data,
    struct olsr_packet_config *config);
static void _cb_packet_event_unicast(int fd, void *data, bool r, bool w);
static void _cb_packet_event_multicast(int fd, void *data, bool r, bool w);
static void _cb_packet_event(int fd, void *data, bool r, bool w, bool mc);
static void _cb_interface_listener(
    struct olsr_interface_listener *l, struct olsr_interface_data *old);

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
    union netaddr_socket *local, struct olsr_interface_data *interf) {
  int s = -1;

  /* Init socket */
  s = os_net_getsocket(local, false, 0, interf, LOG_SOCKET_PACKET);
  if (s < 0) {
    return -1;
  }

  pktsocket->interface = interf;
  pktsocket->scheduler_entry.fd = s;
  pktsocket->scheduler_entry.process = _cb_packet_event_unicast;
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

/**
 * Initialize a new managed packet socket
 * @param managed pointer to packet socket
 */
void
olsr_packet_add_managed(struct olsr_packet_managed *managed) {
  if (managed->config.input_buffer_length == 0) {
    managed->config.input_buffer = input_buffer;
    managed->config.input_buffer_length = sizeof(input_buffer);
  }

  managed->_if_listener.process = _cb_interface_listener;
  managed->_if_listener.name = managed->interface;
}

/**
 * Cleanup an initialized managed packet socket
 * @param managed pointer to packet socket
 * @param forced true if socket should be closed instantly
 */
void
olsr_packet_remove_managed(struct olsr_packet_managed *managed, bool forced) {
  olsr_packet_remove(&managed->socket_v4, forced);
  olsr_packet_remove(&managed->socket_v6, forced);
  olsr_packet_remove(&managed->multicast_v4, forced);
  olsr_packet_remove(&managed->multicast_v6, forced);

  olsr_interface_remove_listener(&managed->_if_listener);
  olsr_acl_remove(&managed->acl);
}

/**
 * Apply a new configuration to a managed socket. This might close and
 * reopen sockets because of changed binding IPs or ports.
 * @param managed pointer to managed packet socket
 * @param config pointer to new configuration
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_packet_apply_managed(struct olsr_packet_managed *managed,
    struct olsr_packet_managed_config *config) {
  olsr_acl_copy(&managed->acl, &config->acl);

  if (strcmp(config->interface, managed->interface) != 0) {
    /* interface changed, remove old listener if necessary */
    olsr_interface_remove_listener(&managed->_if_listener);

    /* copy interface name */
    strscpy(managed->interface, config->interface, sizeof(managed->interface));

    if (*managed->interface) {
      /* create new interface listener */
      olsr_interface_add_listener(&managed->_if_listener);
    }
  }

  return _apply_managed(managed, config);
}

/**
 * Send a packet out over one of the managed sockets, depending on the
 * address family type of the remote address
 * @param managed pointer to managed packet socket
 * @param remote pointer to remote socket
 * @param data pointer to data to send
 * @param length length of data
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_packet_send_managed(struct olsr_packet_managed *managed,
    union netaddr_socket *remote, const void *data, size_t length) {
  if (config_global.ipv4 && netaddr_socket_get_addressfamily(remote) == AF_INET) {
    return olsr_packet_send(&managed->socket_v4, remote, data, length);
  }
  if (config_global.ipv6 && netaddr_socket_get_addressfamily(remote) == AF_INET6) {
    return olsr_packet_send(&managed->socket_v6, remote, data, length);
  }
  return -1;
}

/**
 * Send a packet out over one of the managed sockets, depending on the
 * address family type of the remote address
 * @param managed pointer to managed packet socket
 * @param data pointer to data to send
 * @param length length of data
 * @param af_type address family to send multicast
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_packet_send_managed_multicast(struct olsr_packet_managed *managed,
    const void *data, size_t length, int af_type) {
  if (config_global.ipv4 && af_type == AF_INET) {
    return olsr_packet_send(&managed->socket_v4, &managed->multicast_v4.local_socket, data, length);
  }
  if (config_global.ipv6 && af_type == AF_INET6) {
    return olsr_packet_send(&managed->socket_v6, &managed->multicast_v6.local_socket, data, length);
  }

  errno = ENXIO; /* error, no such device or address */
  return -1;
}

/**
 * Apply a new configuration to an unicast/multicast socket pair
 * @param managed pointer to managed socket
 * @param data pointer to interface to bind sockets, NULL if unbound socket
 * @param sock pointer to unicast packet socket
 * @param bind_ip address to bind unicast socket to
 * @param port source port for unicast socket
 * @param mc_sock pointer to multicast packet socket
 * @param mc_ip multicast address
 * @param mc_port multicast port
 * @param mc_loopback
 * @return
 */
static int
_apply_managed_socketpair(struct olsr_packet_managed *managed,
    struct olsr_interface_data *data,
    struct olsr_packet_socket *sock, struct netaddr *bind_ip, uint16_t port,
    struct olsr_packet_socket *mc_sock, struct netaddr *mc_ip, uint16_t mc_port,
    bool mc_loopback) {
  int result = 0;
  bool real_multicast;

  /* copy unicast port if necessary */
  if (mc_port == 0) {
    mc_port = port;
  }

  /* check if multicast IP is a real multicast (and not a broadcast) */
  real_multicast = netaddr_is_in_subnet(
      mc_ip->type == AF_INET ? &NETADDR_IPV4_MULTICAST : &NETADDR_IPV6_MULTICAST,
      mc_ip);

  if (_apply_managed_socket(sock, bind_ip, port, data, &managed->config)) {
    /* error */
    result = -1;
  }
  else if (real_multicast && data != NULL) {
    /* restrict multicast output to interface */
    os_net_join_mcast_send(sock->scheduler_entry.fd,
        mc_ip, data, mc_loopback, LOG_SOCKET_PACKET);
  }

  if (real_multicast) {
    /* multicast v4*/
    if (_apply_managed_socket(mc_sock, mc_ip, mc_port, data, &managed->config)) {
      /* error */
      result = -1;
    }
    else {
      mc_sock->scheduler_entry.process = _cb_packet_event_multicast;

      /* join multicast group */
      os_net_join_mcast_recv(mc_sock->scheduler_entry.fd,
          mc_ip, data, LOG_SOCKET_PACKET);
    }
  }
  else {
    olsr_packet_remove(mc_sock, true);

    /*
     * initialize anyways because we use it for sending broadcasts with
     * olsr_packet_send_managed_multicast()
     */
    netaddr_socket_init(&mc_sock->local_socket, mc_ip, mc_port);
  }
  return result;
}

/**
 * Apply a new configuration to all attached sockets
 * @param managed pointer to managed socket
 * @param config pointer to configuration
 * @return -1 if an error happened, 0 otherwise
 */
static int
_apply_managed(struct olsr_packet_managed *managed,
    struct olsr_packet_managed_config *config) {
  struct olsr_interface_data *data = NULL;
  int result = 0;

  /* get interface */
  if (managed->_if_listener.interface) {
    data = &managed->_if_listener.interface->data;
  }

  if (config_global.ipv4) {
    if (_apply_managed_socketpair(managed, data,
        &managed->socket_v4, &config->bindto_v4, config->port,
        &managed->multicast_v4, &config->multicast_v4, config->multicast_port,
        config->loop_multicast)) {
      result = -1;
    }
  }
  else {
    olsr_packet_remove(&managed->socket_v4, true);
    olsr_packet_remove(&managed->multicast_v4, true);
  }

  if (config_global.ipv6) {
    if (_apply_managed_socketpair(managed, data,
        &managed->socket_v6, &config->bindto_v6, config->port,
        &managed->multicast_v6, &config->multicast_v6, config->multicast_port,
        config->loop_multicast)) {
      result = -1;
    }
  }
  else {
    olsr_packet_remove(&managed->socket_v6, true);
    olsr_packet_remove(&managed->multicast_v6, true);
  }

  return result;
}


/**
 * Apply new configuration to a managed stream socket
 * @param managed pointer to managed stream
 * @param stream pointer to TCP stream to configure
 * @param bindto local address to bind socket to
 *   set to AF_UNSPEC for simple reinitialization
 * @param port local port number
 * @return -1 if an error happened, 0 otherwise.
 */
static int
_apply_managed_socket(struct olsr_packet_socket *packet,
    struct netaddr *bindto, uint16_t port,
    struct olsr_interface_data *data,
    struct olsr_packet_config *config) {
  union netaddr_socket sock;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif
  if (bindto->type == AF_UNSPEC) {
    /* we are just reinitializing the socket because of an interface event */
    memcpy(&sock, &packet->local_socket, sizeof(sock));
  }
  else if (netaddr_socket_init(&sock, bindto, port)) {
    OLSR_WARN(LOG_SOCKET_PACKET, "Cannot create managed socket address: %s/%u",
        netaddr_to_string(&buf, bindto), port);
    return -1;
  }

  if (list_is_node_added(&packet->node)
      && memcmp(&sock, &packet->local_socket, sizeof(sock)) == 0
      && data == packet->interface) {
    /* nothing changed */
    return 0;
  }

  /* remove old socket */
  olsr_packet_remove(packet, true);

  if (data != NULL && !data->up) {
    return 0;
  }

  /* copy configuration */
  memcpy(&packet->config, config, sizeof(packet->config));

  /* create new socket */
  if (olsr_packet_add(packet, &sock, data)) {
    return -1;
  }

  return 0;
}

/**
 * callback for unicast events in socket scheduler
 * @param fd
 * @param data
 * @param event_read
 * @param event_write
 */
static void
_cb_packet_event_unicast(int fd, void *data, bool event_read, bool event_write) {
  _cb_packet_event(fd, data, event_read, event_write, false);
}

/**
 * callback for multicast events in socket scheduler
 * @param fd
 * @param data
 * @param event_read
 * @param event_write
 */
static void
_cb_packet_event_multicast(int fd, void *data, bool event_read, bool event_write) {
  _cb_packet_event(fd, data, event_read, event_write, true);
}

/**
 * Callback to handle data from the olsr socket scheduler
 * @param fd filedescriptor to read data from
 * @param data custom data pointer
 * @param event_read true if read-event is incoming
 * @param event_write true if write-event is incoming
 */
static void
_cb_packet_event(int fd, void *data, bool event_read, bool event_write, bool multicast) {
  struct olsr_packet_socket *pktsocket = data;
  union netaddr_socket *skt, sock;
  uint16_t length;
  char *pkt;
  int result;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str netbuf;
#endif

  OLSR_DEBUG(LOG_SOCKET_PACKET, "UDP event (%s).", multicast ? "multicast" : "unicast");
  if (event_read) {
    uint8_t *buf;

    /* clear recvfrom memory */
    memset(&sock, 0, sizeof(sock));

    /* handle incoming data */
    buf = pktsocket->config.input_buffer;

    result = os_recvfrom(fd, buf, pktsocket->config.input_buffer_length-1, &sock,
        pktsocket->interface);
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

/**
 * Callbacks for events on the interface
 * @param l
 * @param old
 */
static void
_cb_interface_listener(struct olsr_interface_listener *l,
    struct olsr_interface_data *old __attribute__((unused))) {
  struct olsr_packet_managed *managed;
  struct olsr_packet_managed_config cfg;

  /* calculate managed socket for this event */
  managed = container_of(l, struct olsr_packet_managed, _if_listener);

  memset(&cfg, 0, sizeof(cfg));
  _apply_managed(managed, &cfg);
}
