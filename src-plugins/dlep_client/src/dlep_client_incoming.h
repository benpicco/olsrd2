/*
 * dlep_client_incoming.h
 *
 *  Created on: May 3, 2012
 *      Author: rogge
 */

#ifndef DLEP_CLIENT_INCOMING_H_
#define DLEP_CLIENT_INCOMING_H_

#include "common/common_types.h"
#include "common/netaddr.h"
#include "core/olsr_packet_socket.h"

void dlep_client_incoming_init(void);
void dlep_client_incoming_cleanup(void);

void _cb_receive_dlep(struct olsr_packet_socket *,
      union netaddr_socket *from, size_t length);

#endif /* DLEP_CLIENT_INCOMING_H_ */
