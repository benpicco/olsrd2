
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

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"
#include "common/list.h"
#include "common/netaddr.h"
#include "core/olsr_class.h"
#include "core/olsr_logging.h"
#include "core/olsr_timer.h"
#include "rfc5444/rfc5444.h"

#include "nhdp/nhdp_db.h"
#include "nhdp/nhdp_domain.h"

#include "olsrv2_originator.h"
#include "olsrv2/olsrv2_tc.h"
#include "olsrv2/olsrv2_routing.h"

static struct olsrv2_routing_entry *_add_entry(struct netaddr *prefix);
static void _remove_entry(struct olsrv2_routing_entry *);
static void _prepare_routes(struct nhdp_domain *);
static void _handle_working_queue(struct nhdp_domain *);
static void _update_routes(struct nhdp_domain *);
static void _cb_trigger_dijkstra(void *);

static struct olsr_class _rtset_entry = {
  .name = "Olsrv2 Routing Set Entry",
  .size = sizeof(struct olsrv2_routing_entry),
};

/* rate limitation for dijkstra algorithm */
static struct olsr_timer_info _rate_limit_info = {
  .name = "Dijkstra rate limitation",
  .callback = _cb_trigger_dijkstra,
};

static struct olsr_timer_entry _rate_limit_timer = {
  .info = &_rate_limit_info
};

static bool _trigger_dijkstra = false;

/* global datastructure for routing */
struct avl_tree olsrv2_routing_tree;
static struct avl_tree _working_tree;
static struct list_entity _routing_queue;

static enum log_source LOG_OLSRV2_ROUTING = LOG_MAIN;

void
olsrv2_routing_init(void) {
  LOG_OLSRV2_ROUTING = olsr_log_register_source("olsrv2_routing");

  olsr_class_add(&_rtset_entry);
  olsr_timer_add(&_rate_limit_info);

  avl_init(&olsrv2_routing_tree, avl_comp_netaddr, false);
  avl_init(&_working_tree, avl_comp_uint32, true);
  list_init_head(&_routing_queue);
}

void
olsrv2_routing_cleanup(void) {
  struct olsrv2_routing_entry *entry, *e_it;

  avl_for_each_element_safe(&olsrv2_routing_tree, entry, _global_node, e_it) {
    _remove_entry(entry);
  }
  olsr_class_remove(&_rtset_entry);
}

void
olsrv2_routing_update(void) {
  struct nhdp_domain *domain;
  struct olsrv2_routing_entry_data *data;

  if (olsr_timer_is_active(&_rate_limit_timer)) {
    /* trigger dijkstra later */
    _trigger_dijkstra = true;

    OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Delay Dijkstra");
    return;
  }

  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Run Dijkstra");

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    _prepare_routes(domain);

    /* run dijkstra for each topology */
    while (!avl_is_empty(&_working_tree)) {
      _handle_working_queue(domain);
    }

    /* update kernel routes */
    _update_routes(domain);
  }

  while (!list_is_empty(&_routing_queue)) {
    data = list_first_element(&_routing_queue, data, _working_node);

    // TODO: apply route to kernel
    // TODO: removal from the working queue should be async

    if (data->_set) {
      /* add to kernel */
    }
    else {
      /* remove from kernel */
    }

    /* remove from working list */
    list_remove(&data->_working_node);
  }

  /* make sure dijkstra is not called too often */
  olsr_timer_set(&_rate_limit_timer, 250);
}

void
olsrv2_routing_dijkstra_init(struct olsrv2_dijkstra_node *dijkstra) {
  dijkstra->_node.key = &dijkstra->path_cost;
}

static struct olsrv2_routing_entry *
_add_entry(struct netaddr *prefix) {
  struct olsrv2_routing_entry *rtentry;

  rtentry = avl_find_element(&olsrv2_routing_tree, prefix, rtentry, _global_node);
  if (rtentry) {
    return rtentry;
  }

  rtentry = olsr_class_malloc(&_rtset_entry);
  if (rtentry == NULL) {
    return NULL;
  }

  memcpy(&rtentry->destination, prefix, sizeof(struct netaddr));
  rtentry->_global_node.key = &rtentry->destination;

  avl_insert(&olsrv2_routing_tree, &rtentry->_global_node);
  return rtentry;
}

static void
_remove_entry(struct olsrv2_routing_entry *entry) {
  avl_remove(&olsrv2_routing_tree, &entry->_global_node);
  olsr_class_free(&_rtset_entry, entry);
}

static void
_insert_into_working_tree(struct olsrv2_tc_target *target,
    struct nhdp_neighbor *neigh,
    uint32_t pathcost, uint8_t distance, bool single_hop) {
  struct olsrv2_dijkstra_node *node;
  struct netaddr_str buf;

  node = &target->_dijkstra;
  if (node->first_hop != NULL) {
    /* already hooked into dijkstra ! */
    avl_remove(&_working_tree, &node->_node);
  }

  if (node->path_cost <= pathcost || node->local) {
    /* current target is better or it is ourselves */
    return;
  }

  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Add target %s with pastcost %u to working queue",
      netaddr_to_string(&buf, &target->addr), node->path_cost);

  node->path_cost = pathcost;
  node->first_hop = neigh;
  node->distance = distance;
  node->single_hop = single_hop;

  avl_insert(&_working_tree, &node->_node);
}

static void
_prepare_routes(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry_data *rtdata;
  struct olsrv2_routing_entry *rtentry;
  struct olsrv2_tc_endpoint *end;
  struct olsrv2_tc_node *node;
  struct nhdp_neighbor *neigh;

  /* mark all current routing entries as 'unchanged' */
  avl_for_each_element(&olsrv2_routing_tree, rtentry, _global_node) {
    rtdata = &rtentry->data[domain->index];

    rtdata->_updated = false;
    rtdata->_old_if_index = rtdata->if_index;
    rtdata->_old_distance = rtdata->distance;
    memcpy(&rtdata->_old_next_hop, &rtdata->next_hop, sizeof(struct netaddr));
  }

  avl_for_each_element(&olsrv2_tc_tree, node, _originator_node) {
    node->target._dijkstra.first_hop = NULL;
    node->target._dijkstra.path_cost = RFC5444_METRIC_INFINITE_PATH;
    node->target._dijkstra.local =
        olsrv2_originator_is_local(&node->target.addr);
  }

  avl_for_each_element(&olsrv2_tc_endpoint_tree, end, _node) {
    end->target._dijkstra.first_hop = NULL;
    end->target._dijkstra.path_cost = RFC5444_METRIC_INFINITE_PATH;
  }

  /* initialize Dijkstra working queue with one-hop neighbors */
  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
    if (neigh->symmetric > 0
        && netaddr_get_address_family(&neigh->originator) != AF_UNSPEC
        && (node = olsrv2_tc_node_get(&neigh->originator)) != NULL) {
      /* found node for neighbor, add to worker list */
      _insert_into_working_tree(&node->target, neigh,
          nhdp_domain_get_neighbordata(domain, neigh)->metric.out, 0, true);
    }
  }
}

static void
_handle_working_queue(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry_data *rtdata;
  struct olsrv2_routing_entry *rtentry;
  struct olsrv2_tc_target *target;
  struct nhdp_neighbor *first_hop;
  struct nhdp_neighbor_domaindata *neighdata;

  struct olsrv2_tc_node *tc_node;
  struct olsrv2_tc_edge *tc_edge;
  struct olsrv2_tc_attached_endpoint *tc_attached;

  struct netaddr_str buf;

  /* get tc target */
  target = avl_first_element(&_working_tree, target, _dijkstra._node);

  /* remove current node from working tree */
  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Remove node %s from working queue",
      netaddr_to_string(&buf, &target->addr));
  avl_remove(&_working_tree, &target->_dijkstra._node);

  /* add routing entry */
  rtentry = _add_entry(&target->addr);
  if (rtentry == NULL) {
    return;
  }

  /* get domain specific routing data */
  rtdata = &rtentry->data[domain->index];

  /* get neighbor and its domain specific data */
  first_hop = target->_dijkstra.first_hop;
  neighdata = nhdp_domain_get_neighbordata(domain, first_hop);

  /* fill rt_entry */
  rtdata->if_index = neighdata->best_link_ifindex;
  memcpy(&rtdata->next_hop, &neighdata->best_link->if_addr,
      sizeof(struct netaddr));
  rtdata->cost = target->_dijkstra.path_cost;
  rtdata->distance = target->_dijkstra.distance;
  rtdata->single_hop = target->_dijkstra.single_hop;

  /* mark as updated */
  rtdata->_updated = true;

  if (target->type == OLSRV2_NODE_TARGET) {
    /* calculate pointer of olsrv2_tc_node */
    tc_node = container_of(target, struct olsrv2_tc_node, target);

    /* iterate over edges */
    avl_for_each_element(&tc_node->_edges, tc_edge, _node) {
      if (tc_edge->cost[domain->index] < RFC5444_METRIC_INFINITE) {
        /* add new tc_node to working tree */
        _insert_into_working_tree(&tc_edge->dst->target, first_hop,
            target->_dijkstra.path_cost + tc_edge->cost[domain->index],
            0, false);
      }
    }

    /* iterate over attached networks and addresses */
    avl_for_each_element(&tc_node->_endpoints, tc_attached, _src_node) {
      /* add attached network or address to working tree */
      _insert_into_working_tree(&tc_attached->dst->target, first_hop,
          target->_dijkstra.path_cost + tc_attached->cost[domain->index],
          tc_attached->distance[domain->index], false);
    }
  }

  /* cleanup temporary dijkstra data */
  target->_dijkstra.first_hop = NULL;
}

static void
_update_routes(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry *rtentry, *rt_it;
  struct olsrv2_routing_entry_data *data;
  struct netaddr_str nbuf1, nbuf2;

  avl_for_each_element_safe(
      &olsrv2_routing_tree, rtentry, _global_node, rt_it) {
    /* get pointer to domain relevant data */
    data = &rtentry->data[domain->index];

    if (data->_updated) {
      if (data->_set
          && data->_old_if_index == data->if_index
          && data->_old_distance == data->distance
          && netaddr_cmp(&data->_old_next_hop, &data->next_hop) == 0) {
        /* no change, ignore this entry */
        continue;
      }

      /* entry is new or was updated */
      data->_old_if_index = data->if_index;
      data->_old_distance = data->distance;
      memcpy(&data->_old_next_hop, &data->next_hop, sizeof(struct netaddr));

      data->_set = true;

      OLSR_INFO(LOG_OLSRV2_ROUTING,
          "Route %s over if_index %u to nexthop %s",
          netaddr_to_string(&nbuf1, &rtentry->destination),
          data->if_index,
          netaddr_to_string(&nbuf2, &data->next_hop));

      if (data->single_hop) {
        /* insert/update single-hop routes early */
        list_add_head(&_routing_queue, &data->_working_node);
      }
      else {
        /* insert/update multi-hop routes late */
        list_add_tail(&_routing_queue, &data->_working_node);
      }
    }
    else if (data->_set) {
      OLSR_INFO(LOG_OLSRV2_ROUTING,
          "Remove route %s over if_index %u to nexthop %s",
          netaddr_to_string(&nbuf1, &rtentry->destination),
          data->if_index,
          netaddr_to_string(&nbuf2, &data->next_hop));

      data->_set = false;

      if (!data->single_hop) {
        /* remove single-hop routes late */
        list_add_head(&_routing_queue, &data->_working_node);
      }
      else {
        /* remove multi-hop routes early */
        list_add_tail(&_routing_queue, &data->_working_node);
      }
    }
  }
}

static void
_cb_trigger_dijkstra(void *unused __attribute__((unused))) {
  if (_trigger_dijkstra) {
    _trigger_dijkstra = false;
    olsrv2_routing_update();
  }
}
