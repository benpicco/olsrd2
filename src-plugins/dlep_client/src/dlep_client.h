/*
 * dlep_client.h
 *
 *  Created on: May 3, 2012
 *      Author: rogge
 */

#ifndef DLEP_CLIENT_H_
#define DLEP_CLIENT_H_

#include "common/common_types.h"
#include "common/avl.h"
#include "common/netaddr.h"
#include "packetbb/pbb_writer.h"
#include "core/olsr_logging.h"
#include "core/olsr_packet_socket.h"
#include "core/olsr_timer.h"

/* definitions */
struct _dlep_config {
  struct olsr_packet_managed_config socket;
  char peer_type[81];

  uint64_t connect_interval, connect_validity;
};

struct _dlep_session {
  struct avl_node _node;

  union netaddr_socket interface_socket;
  struct pbb_writer_interface out_if;

  struct netaddr radio_mac;
  struct olsr_timer_entry interface_vtime;
  uint16_t seqno;
};

extern struct _dlep_config _config;
extern struct avl_tree _session_tree;
extern enum log_source LOG_DLEP_CLIENT;

struct _dlep_session *dlep_add_interface_session(
    union netaddr_socket *peer_socket,
    struct netaddr *radio_mac, uint64_t vtime);


#endif /* DLEP_CLIENT_H_ */
