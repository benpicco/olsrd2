
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
static void _handle_nhdp_routes(struct nhdp_domain *);
static void _calculate_routing_queue(struct nhdp_domain *);
static void _cb_trigger_dijkstra(void *);
static void _cb_nhdp_update(struct nhdp_domain *, struct nhdp_neighbor *);

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

/* callback for NHDP domain events */
static struct nhdp_domain_listener _nhdp_listener = {
  .update = _cb_nhdp_update,
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

  nhdp_domain_listener_add(&_nhdp_listener);
}

void
olsrv2_routing_cleanup(void) {
  struct olsrv2_routing_entry *entry, *e_it;

  nhdp_domain_listener_remove(&_nhdp_listener);

  avl_for_each_element_safe(&olsrv2_routing_tree, entry, _node, e_it) {
    _remove_entry(entry);
  }
  olsr_class_remove(&_rtset_entry);
}

void
olsrv2_routing_trigger_update(void) {
  if (olsr_timer_is_active(&_rate_limit_timer)) {
    /* we are in the delay interval between two dijkstras */
    _trigger_dijkstra = true;
  }
  else {
    /* trigger as soon as we hit the next time slice */
    olsr_timer_set(&_rate_limit_timer, 1);
  }
}

void
olsrv2_routing_force_update(bool skip_wait) {
  struct nhdp_domain *domain;
  struct olsrv2_routing_entry_data *data;

  if (olsr_timer_is_active(&_rate_limit_timer)) {
    if (!skip_wait) {
      /* trigger dijkstra later */
      _trigger_dijkstra = true;

      OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Delay Dijkstra");
      return;
    }
    olsr_timer_stop(&_rate_limit_timer);
  }

  OLSR_DEBUG(LOG_OLSRV2_ROUTING, "Run Dijkstra");

  list_for_each_element(&nhdp_domain_list, domain, _node) {
    _prepare_routes(domain);

    /* run dijkstra for each topology */
    while (!avl_is_empty(&_working_tree)) {
      _handle_working_queue(domain);
    }

    /* check if direct one-hop routes are quicker */
    _handle_nhdp_routes(domain);

    /* update kernel routes */
    _calculate_routing_queue(domain);
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
  int i;

  rtentry = avl_find_element(&olsrv2_routing_tree, prefix, rtentry, _node);
  if (rtentry) {
    return rtentry;
  }

  rtentry = olsr_class_malloc(&_rtset_entry);
  if (rtentry == NULL) {
    return NULL;
  }

  memcpy(&rtentry->destination, prefix, sizeof(struct netaddr));
  rtentry->_node.key = &rtentry->destination;

  /* initialize path costs */
  for (i=0; i<NHDP_MAXIMUM_DOMAINS; i++) {
    rtentry->data[i].cost = RFC5444_METRIC_INFINITE_PATH;
  }

  avl_insert(&olsrv2_routing_tree, &rtentry->_node);
  return rtentry;
}

static void
_remove_entry(struct olsrv2_routing_entry *entry) {
  avl_remove(&olsrv2_routing_tree, &entry->_node);
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
  avl_for_each_element(&olsrv2_routing_tree, rtentry, _node) {
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
  struct olsrv2_tc_attachment *tc_attached;

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
_handle_nhdp_routes(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry *rtentry;
  struct nhdp_neighbor_domaindata *neigh_data;
  struct nhdp_neighbor *neigh;
  struct nhdp_naddr *naddr;
  struct nhdp_l2hop *l2hop;
  struct nhdp_link *lnk;

  list_for_each_element(&nhdp_neigh_list, neigh, _global_node) {
    uint32_t neighcost;
    bool improved;

    /* get linkcost to neighbor */
    neigh_data = nhdp_domain_get_neighbordata(domain, neigh);
    neighcost = neigh_data->metric.out;

    /* remember if we improved the link neighcost */
    improved = false;

    /* make sure all addresses of the neighbor are better than our direct link */
    avl_for_each_element(&neigh->_neigh_addresses, naddr, _neigh_node) {
      rtentry = _add_entry(&naddr->neigh_addr);
      if (rtentry == NULL || rtentry->data[domain->index].cost <= neighcost) {
        /* next neighbor address */
        continue;
      }

      /* the direct link is better than the dijkstra calculation */
      rtentry->data[domain->index].cost = neighcost;
      rtentry->data[domain->index].distance = 0;
      rtentry->data[domain->index].if_index = neigh_data->best_link_ifindex;
      rtentry->data[domain->index].single_hop = true;

      netaddr_invalidate(&rtentry->data[domain->index].next_hop);

      /* found better route, remember for 2-hop test */
      improved = true;
    }

    if (!improved) {
      /* next neighbor */
      continue;
    }

    list_for_each_element(&neigh->_links, lnk, _neigh_node) {
      avl_for_each_element(&lnk->_2hop, l2hop, _link_node) {
        uint32_t l2hop_pathcost;

        /* get new pathcost to 2hop neighbor */
        l2hop_pathcost =
            neighcost + nhdp_domain_get_l2hopdata(domain, l2hop)->metric.out;

        rtentry = _add_entry(&l2hop->twohop_addr);
        if (rtentry == NULL
            || rtentry->data[domain->index].cost <= l2hop_pathcost) {
          /* next 2hop address */
          continue;
        }

        /* the 2-hop route is better than the dijkstra calculation */
        rtentry->data[domain->index].cost = l2hop_pathcost;
        rtentry->data[domain->index].distance = 0;
        rtentry->data[domain->index].if_index = neigh_data->best_link_ifindex;
        rtentry->data[domain->index].single_hop = false;

        memcpy(&rtentry->data[domain->index].next_hop,
            &neigh_data->best_link->if_addr, sizeof(struct netaddr));
      }
    }
  }
}

static void
_calculate_routing_queue(struct nhdp_domain *domain) {
  struct olsrv2_routing_entry *rtentry, *rt_it;
  struct olsrv2_routing_entry_data *data;
  struct netaddr_str nbuf1, nbuf2;

  avl_for_each_element_safe(
      &olsrv2_routing_tree, rtentry, _node, rt_it) {
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
    olsrv2_routing_force_update(false);
  }
}

static void
_cb_nhdp_update(struct nhdp_domain *domain __attribute__((unused)),
    struct nhdp_neighbor *neigh __attribute__((unused))) {
  olsrv2_routing_trigger_update();
}
