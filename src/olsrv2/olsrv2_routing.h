
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

#ifndef OONFV2_ROUTING_H_
#define OONFV2_ROUTING_H_

#include "common/avl.h"
#include "common/common_types.h"
#include "common/list.h"
#include "common/netaddr.h"

#include "subsystems/os_routing.h"

#include "nhdp/nhdp.h"
#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"

/* representation of a node in the dijkstra tree */
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

/* representation of one target in the routing entry set */
struct olsrv2_routing_entry {
  /* Settings for the kernel route */
  struct os_route route;

  /* nhdp domain of route */
  struct nhdp_domain *domain;

  /* path cost to reach the target */
  uint32_t cost;

  /*
   * true if the entry represents a route that should be in the kernel,
   * false if the entry should be removed from the kernel
   */
  bool set;

  /* true if this route is being processed by the kernel at the moment */
  bool in_processing;

  /* forwarding information before the current dijkstra run */
  unsigned _old_if_index;
  struct netaddr _old_next_hop;
  uint8_t _old_distance;

  /* hook into working queues */
  struct list_entity _working_node;

  /* global node */
  struct avl_node _node;
};

/* routing domain specific parameters */
struct olsrv2_routing_domain {
  /* true if IPv4 routes should set a source IP */
  bool use_srcip_in_routes;

  /* protocol number for routes */
  uint8_t protocol;

  /* routing table number for routes */
  uint8_t table;

  /* metric value that should be used for routes */
  uint8_t distance;
};

EXPORT extern struct avl_tree olsrv2_routing_tree[NHDP_MAXIMUM_DOMAINS];

void olsrv2_routing_init(void);
void olsrv2_routing_initiate_shutdown(void);
void olsrv2_routing_cleanup(void);

void olsrv2_routing_dijkstra_node_init(struct olsrv2_dijkstra_node *);

EXPORT void olsrv2_routing_set_domain_parameter(struct nhdp_domain *domain,
    struct olsrv2_routing_domain *parameter);

EXPORT void olsrv2_routing_force_update(bool skip_wait);
EXPORT void olsrv2_routing_trigger_update(void);

EXPORT const struct olsrv2_routing_domain *
    olsrv2_routing_get_parameters(struct nhdp_domain *);

#endif /* OONFV2_ROUTING_SET_H_ */
