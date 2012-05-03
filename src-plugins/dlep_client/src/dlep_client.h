
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
