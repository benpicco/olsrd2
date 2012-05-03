/*
 * dlep_session.h
 *
 *  Created on: May 3, 2012
 *      Author: rogge
 */

#ifndef DLEP_SESSION_H_
#define DLEP_SESSION_H_

#include "common/common_types.h"
#include "common/avl.h"
#include "common/netaddr.h"
#include "packetbb/pbb_writer.h"
#include "core/olsr_logging.h"
#include "core/olsr_packet_socket.h"
#include "core/olsr_timer.h"

struct _dlep_session {
  struct avl_node _node;

  union netaddr_socket router_socket;
  struct netaddr radio_mac;
  struct olsr_timer_entry router_vtime;
};

/* definitions */
struct _dlep_config {
  struct olsr_packet_managed_config socket;

  char peer_type[81];

  uint64_t discovery_interval, discovery_validity;
  uint64_t metric_interval, metric_validity;

  bool always_send;
};

extern struct _dlep_config _config;
extern struct avl_tree _session_tree;
extern enum log_source LOG_DLEP_SERVICE;

struct _dlep_session *dlep_add_router_session(
    union netaddr_socket *peer_socket, uint64_t vtime);

void _cb_sendMulticast(struct pbb_writer *writer,
    struct pbb_writer_interface *interf,
    void *ptr, size_t len);
#endif /* DLEP_SESSION_H_ */
