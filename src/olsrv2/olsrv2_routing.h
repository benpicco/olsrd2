
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2013, the olsr.org team - see HISTORY file
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

#ifndef OLSRV2_ROUTING_H_
#define OLSRV2_ROUTING_H_

#include "common/avl.h"
#include "common/common_types.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"

struct olsrv2_dijkstra_node {
  /* hook into the working list of the dijkstra */
  struct avl_node _node;

  /* total path cost */
  uint32_t path_cost;

  /* hopcount to be inserted into the route */
  uint8_t distance;

  /* pointer to nhpd neighbor that represents the first hop */
  struct nhdp_neighbor *first_hop;

  /* true if route is single-hop */
  bool single_hop;

  /* true if this node is ourself */
  bool local;
};

struct olsrv2_routing_entry_data {
  /* interface index through which this target can be reached */
  unsigned if_index;

  /* address of the next hop to reach this target */
  struct netaddr next_hop;

  /* path cost to reach the target */
  uint32_t cost;

  /* hopcount distance the target should have */
  uint8_t distance;

  /* true if route is single-hop */
  bool single_hop;

  /* true if nexthop was changed in current dijkstra run */
  bool _updated;

  /* true if this routing entry is active */
  bool _set;

  /* forwarding information before the current dijkstra run */
  unsigned _old_if_index;
  struct netaddr _old_next_hop;
  uint8_t _old_distance;

  /* back pointer to routing entry */
  struct olsrv2_routing_entry *rtentry;

  /* hook into routing queue */
  struct list_entity _working_node;
};

struct olsrv2_routing_entry {
  struct netaddr destination;

  /* parameters to reach the destination in each topology */
  struct olsrv2_routing_entry_data data[NHDP_MAXIMUM_DOMAINS];

  /* global node */
  struct avl_node _node;
};

EXPORT extern struct avl_tree olsrv2_routing_tree;

void olsrv2_routing_init(void);
void olsrv2_routing_cleanup(void);

void olsrv2_routing_dijkstra_init(struct olsrv2_dijkstra_node *);

EXPORT void olsrv2_routing_force_update(bool skip_wait);
EXPORT void olsrv2_routing_trigger_update(void);

#endif /* OLSRV2_ROUTING_SET_H_ */
